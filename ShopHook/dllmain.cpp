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
#include <set>
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

// Documented FF7 (ff7_en.exe) inventory routines (Qhimm "Custom Game Settings"
// RE thread + FFNx externals). All sit in the stable 0x6CB… range the DLL already
// treats as constant live VAs (cf. current_module 0xCBF9DC). We hook the item /
// materia *grant* functions to suppress AP-token purchases (gil is deducted by a
// SEPARATE DecreaseGil call, so the player still pays).
static const uint32_t ADDR_ADD_ITEM    = 0x6CBFFA;  // AddItems(DWORD (qty<<9)|item_id)
static const uint32_t ADDR_ADD_MATERIA = 0x6CBCF3;  // add materia (materia id)

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

// Kernel text section ids the shop grid uses (mirrors the client's
// _token_section_index): section 4 = carried items (composite id), 13 = materia.
static const uint32_t KTEXT_ITEM    = 4;
static const uint32_t KTEXT_MATERIA = 13;

static inline uint32_t NameKey(uint32_t section, uint32_t index) {
    return (section << 16) | (index & 0xFFFF);
}

// Per-slot description overrides (a3==0), keyed identically to g_names. Populated
// from the optional "|<description>" suffix on each shop_ap.txt line.
static std::map<uint32_t, std::string> g_descs;

// Reserved AP-token ids by inventory space, derived from shop_ap.txt sections:
//   section 4  -> item-space  (composite id, detected in AddItems)
//   section 13 -> materia-space (materia id, detected in add-materia)
// Buying one of these is an AP shop purchase: we suppress the grant + signal it.
static std::set<uint32_t> g_itemTokens;
static std::set<uint32_t> g_materiaTokens;

static void LoadConfig(const std::string& dir) {
    std::ifstream f(dir + "shop_ap.txt");
    if (!f) { LogLine("no shop_ap.txt (display passthrough)\n"); return; }
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key  = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back()=='\r' || value.back()=='\n')) value.pop_back();
        // Optional "<name>|<description>" payload. Split on the FIRST '|'.
        std::string name = value, desc;
        size_t bar = value.find('|');
        if (bar != std::string::npos) {
            name = value.substr(0, bar);
            desc = value.substr(bar + 1);
        }
        uint32_t section, index;
        size_t colon = key.find(':');
        if (colon != std::string::npos) {            // new "<section>:<index>"
            section = strtoul(key.substr(0, colon).c_str(), nullptr, 0);
            index   = strtoul(key.substr(colon + 1).c_str(), nullptr, 0);
        } else {                                     // legacy "<itemId>" => item section
            section = 4;
            index   = strtoul(key.c_str(), nullptr, 0);
        }
        const uint32_t k = NameKey(section, index);
        g_names[k] = EncodeFF7(name);
        if (!desc.empty()) g_descs[k] = EncodeFF7(desc);
        // Track the reserved token id in its inventory space for purchase suppression.
        if (section == KTEXT_MATERIA) g_materiaTokens.insert(index & 0xFFFF);
        else                         g_itemTokens.insert(index & 0xFFFF);
    }
    LogLine("loaded %zu name + %zu description override(s); %zu item + %zu materia token(s)\n",
            g_names.size(), g_descs.size(), g_itemTokens.size(), g_materiaTokens.size());
}

// ── get_kernel_text hook: override item names in the shop grid ────────────────
using GetKernelText_t = char*(__cdecl*)(uint32_t, uint32_t, uint32_t);
static GetKernelText_t oGetKernelText = nullptr;

// FF7 live game module (== ff7-ultima current_module, verified for goal detection:
// its game_moment matches our savemap+0xBA4). Field=1, Battle=2, World=3, Menu=5.
static const uint32_t kCurrentModuleAddr = 0xCBF9DC;
static const uint8_t  kModuleMenu        = 5;
static inline bool InMenu() {
    return *reinterpret_cast<volatile uint8_t*>(kCurrentModuleAddr) == kModuleMenu;
}

// "In shop" flag: true only while the resolved shop loop (menu_shop_loop) is on the
// stack. The shop's name draws happen inside it, so this lets us override AP shop
// slot names ONLY on the shop screen — NOT in the equip/materia/item menus, where
// the same kernel ids belong to gear/items the player actually owns (which would
// otherwise display the AP name, e.g. an equipped weapon showing "A Holy Torch @ ..").
// Cosmetic-only: if it never goes true, the shop just shows real item names; the
// purchase suppression + checks are unaffected (those stay gated on InMenu()).
static volatile bool g_inShop = false;
using ShopLoop_t = int(__cdecl*)(uint32_t, uint32_t, uint32_t, uint32_t);
static ShopLoop_t oShopLoop = nullptr;
static int __cdecl hkShopLoop(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    // menu_shop_loop is a __cdecl menu function; declaring 4 pass-through args is
    // safe under caller-cleanup regardless of its real arity (<=4).
    const bool prev = g_inShop;
    g_inShop = true;
    const int rv = oShopLoop ? oShopLoop(a, b, c, d) : 0;
    g_inShop = prev;
    return rv;
}

// get_kernel_text a3 argument: 8 = name, 0 = description (per this file's header).
static const uint32_t KTEXT_A3_NAME = 8;
static const uint32_t KTEXT_A3_DESC = 0;

// ── materia purchase discriminator (gil-drop) ────────────────────────────────
// The materia-grant routine (0x6CBCF3) is also reached while merely hovering a
// shop slot, so we can't suppress+signal unconditionally (that fires the check on
// hover). The reliable difference is GIL: a real buy decreases party gil, a hover
// does not. We don't know whether the game deducts gil before or after the grant,
// so we detect BOTH: (a) "immediate" — at the grant the gil is already below the
// previous token-grant call's gil (pay-before-grant); (b) "deferred" — we arm a
// pending check and the per-render sampler fires it when gil drops below the armed
// baseline within a short window (pay-after-grant). Hover never drops gil, so it
// never fires. The reserved token is ALWAYS suppressed (never enters inventory).
static const uint32_t kGilAddr = 0xDBFD38 + 0x0B7C;   // party gil (== client GIL_OFFSET)
static bool ReadGil(uint32_t& out) {
    __try { out = *reinterpret_cast<volatile uint32_t*>(kGilAddr); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static const uint32_t kGilSentinel = 0xFFFFFFFFu;
static uint32_t g_lastGil = kGilSentinel;  // party gil sampled every render (immediate-buy baseline)
static int      g_pendingToken   = -1;            // token id awaiting a deferred gil-drop
static uint32_t g_pendingGil     = 0;             // gil baseline when the pending check armed
static DWORD    g_pendingTick    = 0;             // GetTickCount() when armed
static const DWORD kPendingWindowMs = 2500;       // hover→buy must drop gil within this

static void SignalPurchase(uint32_t section, uint32_t index);   // fwd decl

static char* __cdecl hkGetKernelText(uint32_t section, uint32_t index, uint32_t a3) {
    if (InMenu()) {
        uint32_t g;
        if (ReadGil(g)) {
            // Resolve a pending materia check the moment gil drops below the armed
            // baseline (pay-after-grant); expire it if no drop within the window.
            if (g_pendingToken >= 0) {
                if (g < g_pendingGil) {
                    SignalPurchase(KTEXT_MATERIA, static_cast<uint32_t>(g_pendingToken));
                    g_pendingToken = -1;
                } else if (GetTickCount() - g_pendingTick > kPendingWindowMs) {
                    g_pendingToken = -1;   // no gil drop in window -> it was a hover
                }
            }
            // Rolling per-render gil baseline for the immediate (pay-before-grant)
            // check. Sampling every frame means a HOVER reads gil == baseline and is
            // never mistaken for a buy — even if an earlier unrelated purchase already
            // dropped gil this shop visit (the old per-token baseline misfired there).
            g_lastGil = g;
        }
    } else {
        // Left the menu: clear materia state so a stale baseline can't misfire next time.
        g_lastGil      = kGilSentinel;
        g_pendingToken = -1;
    }
    // Override shop slot names/descriptions ONLY while the shop screen is open
    // (g_inShop, set by the menu_shop_loop bracket). Item-space tokens are real
    // weapon/armor/item ids the player can own & equip; gating on InMenu() alone
    // made an equipped token show its AP name in the equip/materia menu. Restricting
    // to the shop screen shows AP names where they belong and real names everywhere
    // else. (If g_inShop never trips, the shop harmlessly shows real item names.)
    if (g_inShop) {
        const uint32_t k = NameKey(section, index);
        if (a3 == KTEXT_A3_NAME) {
            auto it = g_names.find(k);
            if (it != g_names.end())
                return const_cast<char*>(it->second.c_str());   // custom AP name
        } else if (a3 == KTEXT_A3_DESC) {
            auto it = g_descs.find(k);
            if (it != g_descs.end())
                return const_cast<char*>(it->second.c_str());   // custom AP description
        }
    }
    return oGetKernelText ? oGetKernelText(section, index, a3) : nullptr;
}

// ── shop-purchase suppression: hook the item/materia grant routines ───────────
// When a reserved AP token is granted while the Menu module is active, it means
// the player just bought an AP shop slot. We DON'T grant the item (so it never
// enters inventory) and instead append "<section>:<index>" to shop_buys.txt for
// the AP client to consume and fire the location check. Gil is deducted by a
// separate DecreaseGil call, so the player still pays.
static std::string g_buysPath;   // <exe dir>/shop_buys.txt

static void SignalPurchase(uint32_t section, uint32_t index) {
    if (g_buysPath.empty()) return;
    // De-dup: one materia buy can be detected twice in the same instant — once by
    // the deferred render-sampler (gil-drop observed) and once by the immediate
    // grant-call path. Both map to the same location, so collapse repeats of the
    // same (section,index) within a short window into a single signal.
    static uint32_t s_lastKey  = 0xFFFFFFFFu;
    static DWORD    s_lastTick  = 0;
    const uint32_t key = (section << 16) | (index & 0xFFFF);
    const DWORD now = GetTickCount();
    if (key == s_lastKey && now - s_lastTick < 1500) return;
    s_lastKey = key; s_lastTick = now;
    FILE* fp = fopen(g_buysPath.c_str(), "a");
    if (!fp) { LogLine("shop_buys.txt append failed\n"); return; }
    fprintf(fp, "%u:%u\n", section, index);
    fclose(fp);
    LogLine("AP shop purchase signalled: %u:%u\n", section, index);
}

// AddItems(DWORD word) where word = (qty<<9) | item_id. __cdecl per the FF7 ABI
// for this menu routine; confirmed via logging before suppression is enabled.
using AddItem_t = void(__cdecl*)(uint32_t);
static AddItem_t oAddItem = nullptr;

static void __cdecl hkAddItem(uint32_t word) {
    const uint32_t id = word & 0x1FF;
    if (InMenu() && g_itemTokens.count(id)) {
        SignalPurchase(KTEXT_ITEM, id);   // suppress the grant entirely
        return;
    }
    if (oAddItem) oAddItem(word);
}

// add-materia routine: takes the materia id (low byte). __cdecl; confirmed via log.
using AddMateria_t = void(__cdecl*)(uint32_t);
static AddMateria_t oAddMateria = nullptr;

static void __cdecl hkAddMateria(uint32_t mid) {
    const uint32_t id = mid & 0xFF;
    // The grant routine is reached on HOVER as well as on buy, so suppressing+
    // signalling on every call fires the check on hover. Distinguish by gil drop
    // (see the discriminator notes above): a buy decreases gil, a hover does not.
    if (InMenu() && g_materiaTokens.count(id)) {
        uint32_t gil = 0;
        const bool have = ReadGil(gil);
        // (a) pay-before-grant: gil dropped since the last render frame. A hover
        // never changes gil, so gil == g_lastGil and this stays false (the fix for
        // hover-firing — the old per-token baseline went stale after other buys).
        const bool immediate = have && g_lastGil != kGilSentinel && gil < g_lastGil;
        if (immediate) {
            LogLine("addMateria token %u: gil %u < lastGil %u -> BUY (immediate)\n",
                    id, gil, g_lastGil);
            g_pendingToken = -1;
            SignalPurchase(KTEXT_MATERIA, id);
        } else if (have) {
            // (b) pay-after-grant / first sight: arm a deferred check; the render
            // sampler fires it if gil drops within the window, else treats it as hover.
            g_pendingToken = static_cast<int>(id);
            g_pendingGil   = gil;
            g_pendingTick  = GetTickCount();
            LogLine("addMateria token %u: gil %u -> armed pending (await gil drop)\n", id, gil);
        }
        return;   // ALWAYS suppress the reserved token grant (never enters inventory)
    }
    if (oAddMateria) oAddMateria(mid);
}

// Sanity-check that an address looks like a function entry before hooking it, so
// an unexpected exe build fails safe (logs a warning) instead of crashing.
static bool LooksLikeCode(uint32_t va) {
    __try {
        volatile uint8_t b = *reinterpret_cast<volatile uint8_t*>(va);
        (void)b;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
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

    g_buysPath = dir + "shop_buys.txt";

    if (MH_Initialize() != MH_OK) { LogLine("MH_Initialize failed\n"); return 1; }
    if (g_get_kernel_text &&
        MH_CreateHook(reinterpret_cast<void*>(g_get_kernel_text), &hkGetKernelText,
                      reinterpret_cast<void**>(&oGetKernelText)) == MH_OK) {
        LogLine("get_kernel_text hooked — shop names/descriptions use shop_ap.txt\n");
    } else {
        LogLine("Failed to hook get_kernel_text\n");
    }

    // Bracket the shop loop so name overrides apply ONLY on the shop screen (g_inShop),
    // not in the equip/materia/item menus where the same ids are the player's own gear.
    if (g_menu_shop_loop && LooksLikeCode(g_menu_shop_loop) &&
        MH_CreateHook(reinterpret_cast<void*>(g_menu_shop_loop), &hkShopLoop,
                      reinterpret_cast<void**>(&oShopLoop)) == MH_OK) {
        LogLine("menu_shop_loop(0x%X) hooked — AP names shown only on the shop screen\n", g_menu_shop_loop);
    } else {
        LogLine("WARN: could not hook menu_shop_loop(0x%X) — shop will show real item names\n", g_menu_shop_loop);
    }

    // Suppress AP-token grants so purchased tokens never enter inventory. Only
    // hook if there are tokens to watch and the target looks like code.
    if (!g_itemTokens.empty() && LooksLikeCode(ADDR_ADD_ITEM) &&
        MH_CreateHook(reinterpret_cast<void*>(ADDR_ADD_ITEM), &hkAddItem,
                      reinterpret_cast<void**>(&oAddItem)) == MH_OK) {
        LogLine("AddItems(0x%X) hooked — item-token grants suppressed\n", ADDR_ADD_ITEM);
    } else if (!g_itemTokens.empty()) {
        LogLine("WARN: could not hook AddItems(0x%X) — item tokens NOT suppressed\n", ADDR_ADD_ITEM);
    }
    if (!g_materiaTokens.empty() && LooksLikeCode(ADDR_ADD_MATERIA) &&
        MH_CreateHook(reinterpret_cast<void*>(ADDR_ADD_MATERIA), &hkAddMateria,
                      reinterpret_cast<void**>(&oAddMateria)) == MH_OK) {
        LogLine("AddMateria(0x%X) hooked — materia-token grants suppressed\n", ADDR_ADD_MATERIA);
    } else if (!g_materiaTokens.empty()) {
        LogLine("WARN: could not hook AddMateria(0x%X) — materia tokens NOT suppressed\n", ADDR_ADD_MATERIA);
    }

    MH_EnableHook(MH_ALL_HOOKS);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
    }
    return TRUE;
}
