// shophook.dll — FF7 (Steam / ff7_en.exe) AP shop hook.
//
// Injection under 7th Heaven is confirmed working. This build does the real
// Tier-3 DISPLAY override: the FF7 shop grid shows custom (AP) item names.
//
// Mechanism (learned empirically, 2026-06-09):
//   get_kernel_text(section, index, a3) is FF7's text lookup.
//     section 4 = item names, index = item ID, a3=8 = name (a3=0 = description).
//   The SHOP grid draws a slot's name via this call from inside the shop loop
//   (menu_shop_loop=0x71AAA3; the name-draw caller observed at 0x71B8AE). So:
//     section==4 && a3==8 && caller in [menu_shop_loop, +0x2000]  ==>  shop name.
//   We return a custom FF7-encoded name for that item id, leaving inventory/
//   battle/other menus untouched.
//
// FF7 text encoding (derived from get_kernel_text output bytes):
//   'A'-'Z' -> 0x21 + (c-'A');  'a'-'z' -> 0x41 + (c-'a');  ' ' -> 0x20;
//   string terminator -> 0xFF.   (digits/punct = TODO, verify in game)
//
// Config: shop_ap.txt next to ff7_en.exe, one entry per line:  <itemId>=<Name>
//   e.g.  0=Bnuy's Potion
// NOTE: keyed by item id only for now (no current-shop-id global exists in FFNx,
// so two shops selling the same item show the same name — next RE step).
//
// Build x86 (see CMakeLists.txt).

#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <map>
#include <string>
#include <fstream>
#include "MinHook.h"

// ── FFNx-style address resolver (ported from FFNx patch.cpp) ───────────────
static uint32_t rel_call(uint32_t base, uint32_t offset) {
    uint16_t instr = *reinterpret_cast<uint16_t*>(base + offset);
    uint8_t size = (instr == 0x15FF) ? 2 : 1;          // FF15 indirect=2, else E8/E9=1
    return base + *reinterpret_cast<uint32_t*>(base + offset + size) + offset + 4 + size;
}
static uint32_t abs_val(uint32_t base, uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(base + offset);
}

static const uint32_t MENU_SUB_71FF95 = 0x71FF95;
static const uint32_t MENU_SUB_6CB56A = 0x6CB56A;

static uint32_t g_get_kernel_text = 0;
static uint32_t g_menu_shop_loop  = 0;

static void ResolveAddresses() {
    g_menu_shop_loop = rel_call(MENU_SUB_71FF95, 0x84);
    uint32_t table = abs_val(MENU_SUB_6CB56A, 0x2EC);
    uint32_t status_menu_sub = reinterpret_cast<uint32_t*>(table)[5];
    uint32_t draw_status = rel_call(status_menu_sub, 0x8E);
    g_get_kernel_text = rel_call(draw_status, 0x10C);
}
static bool TryResolve() {   // SEH wrapper free of C++ objects
    __try { ResolveAddresses(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ── logging ────────────────────────────────────────────────────────────────
static FILE* g_log = nullptr;
static void LogLine(const char* fmt, ...) {
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap); fflush(g_log);
}

// ── FF7 text encoding + name overrides ───────────────────────────────────────
static std::string EncodeFF7(const std::string& s) {
    // FF7 menu/kernel charmap == ASCII - 0x20 across the printable range
    // (space 0x20->0x00, '@'->0x20, 'A'->0x21, 'a'->0x41). Covers letters,
    // digits, apostrophes, punctuation — everything we need for AP names.
    std::string out;
    for (unsigned char c : s)
        if (c >= 0x20 && c <= 0x7E) out += char(c - 0x20);
    out += char(0xFF);   // FF7 string terminator
    return out;
}

// Overrides keyed by (kernel text section << 16 | index). The shop grid draws a
// slot's name via get_kernel_text(section, index, a3=8). Confirmed from logging:
// section 4 = ALL carried items (consumable/weapon/armor/accessory) by composite
// id; section 13 = materia by materia id. The client (FF7Client._token_section_index)
// writes shop_ap.txt with matching <section>:<index> keys.
static std::map<uint32_t, std::string> g_names;

static inline uint32_t NameKey(uint32_t section, uint32_t index) {
    return (section << 16) | (index & 0xFFFF);
}

static void LoadConfig(const std::string& dir) {
    std::ifstream f(dir + "shop_ap.txt");
    if (!f) { LogLine("no shop_ap.txt (display passthrough)\n"); return; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key  = line.substr(0, eq);
        std::string name = line.substr(eq + 1);
        while (!name.empty() && (name.back()=='\r' || name.back()=='\n')) name.pop_back();
        uint32_t section, index;
        size_t colon = key.find(':');
        if (colon != std::string::npos) {            // new "<section>:<index>"
            section = strtoul(key.substr(0, colon).c_str(), nullptr, 0);
            index   = strtoul(key.substr(colon + 1).c_str(), nullptr, 0);
        } else {                                     // legacy "<itemId>" => item section
            section = 4;
            index   = strtoul(key.c_str(), nullptr, 0);
        }
        g_names[NameKey(section, index)] = EncodeFF7(name);
    }
    LogLine("loaded %zu shop name override(s)\n", g_names.size());
}

// ── get_kernel_text hook: override item names in the shop grid ────────────────
using GetKernelText_t = char*(__cdecl*)(uint32_t, uint32_t, uint32_t);
static GetKernelText_t oGetKernelText = nullptr;

static char* __cdecl hkGetKernelText(uint32_t section, uint32_t index, uint32_t a3) {
    uint32_t caller = (uint32_t)(uintptr_t)_ReturnAddress();
    bool inShop = g_menu_shop_loop &&
                  caller >= g_menu_shop_loop && caller < g_menu_shop_loop + 0x2000;
    if (inShop && a3 == 8) {
        auto it = g_names.find(NameKey(section, index));
        if (it != g_names.end())
            return const_cast<char*>(it->second.c_str());   // custom AP name
    }
    return oGetKernelText ? oGetKernelText(section, index, a3) : nullptr;
}

static DWORD WINAPI Init(LPVOID) {
    Sleep(3000);  // let FF7 + FFNx finish loading
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string dir(path); dir = dir.substr(0, dir.find_last_of("\\/") + 1);
    g_log = fopen((dir + "shophook_log.txt").c_str(), "w");

    if (!TryResolve()) { LogLine("ResolveAddresses faulted\n"); return 1; }
    LogLine("Resolved: get_kernel_text=0x%X  menu_shop_loop=0x%X\n",
            g_get_kernel_text, g_menu_shop_loop);
    LoadConfig(dir);

    if (MH_Initialize() != MH_OK) { LogLine("MH_Initialize failed\n"); return 1; }
    if (g_get_kernel_text &&
        MH_CreateHook(reinterpret_cast<void*>(g_get_kernel_text), &hkGetKernelText,
                      reinterpret_cast<void**>(&oGetKernelText)) == MH_OK) {
        MH_EnableHook(MH_ALL_HOOKS);
        LogLine("get_kernel_text hooked — shop names will use shop_ap.txt overrides\n");
    } else {
        LogLine("Failed to hook get_kernel_text\n");
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    }
    return TRUE;
}
