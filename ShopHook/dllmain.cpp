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

// Overrides keyed by (shop_id, section, index). SHOP-ID-AWARE (2026-07-19): the
// same real FF7 item/materia id can be AP stock in one shop and vanilla stock in
// another (or an equippable the player owns) — keying on the open shop id makes
// every override + purchase signal unambiguous. The shop grid draws a slot's name
// via get_kernel_text(section, index, a3=8): section 4 = carried items (composite
// id), 13 = materia. The client writes shop_ap.txt lines <shop>:<section>:<index>=
// <name>[|<desc>].
static const uint32_t KTEXT_ITEM    = 4;
static const uint32_t KTEXT_MATERIA = 13;

// Current open shop id: FF7 stores it here when a shop opens (found by tracing the
// shop-setup fn 0x719D7A -> mov [0xDD4724], eax; shop-active flag at 0xDC3D14).
static const uint32_t CURRENT_SHOP_ID_ADDR = 0xDD4724;

static inline uint32_t CurrentShopId() {
    __try { return *reinterpret_cast<volatile uint32_t*>(CURRENT_SHOP_ID_ADDR); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
}

// 64-bit key: shop_id<<32 | section<<16 | index.
static inline uint64_t SlotKey(uint32_t shop, uint32_t section, uint32_t index) {
    return (static_cast<uint64_t>(shop) << 32) | (section << 16) | (index & 0xFFFF);
}

static std::map<uint64_t, std::string> g_names;   // (shop,section,index) -> AP name
static std::map<uint64_t, std::string> g_descs;   // (shop,section,index) -> AP description
static std::set<uint64_t> g_apSlots;              // the reserved AP cells (buy suppress+signal)

// Sold AP cells (already-checked locations). The client writes shop_sold.txt as
// "<shop>:<section>:<index>" lines and rewrites it as checks fire; the DLL reloads
// it on each shop open and COMPACTS those cells out of the shop's stock so an
// obtained AP item can't be re-bought (no gil waste). Reserved from a fresh static
// shop table each game launch, so removal is re-applied every session.
static std::set<uint64_t> g_soldSlots;
static std::string        g_soldPath;

// Deferred mid-visit removal: a grant hook fires from DEEP inside FF7's buy code,
// so we don't edit the shop table there (the buy flow may re-read the entry it
// just sold). Instead we flag the shop and compact it on the next render frame
// (get_kernel_text), between transactions — the slot vanishes from the grid the
// moment the player returns to it, same visit.
static volatile bool g_removePending = false;
static uint32_t      g_removeShop    = 0;

// FF7 shop inventory table (Hext-applied AP tokens live here): base 0x923418,
// 80 records x 0x54. Record: [u16 type][u8 itemCount][u8 pad][10 x {i32 type
// (0=item/1=materia), u16 index, u16 pad}].
static const uint32_t SHOP_TABLE = 0x923418;

// Shop buy-list selection state (confirmed live 2026-07-21 by stepping the cursor
// in shop 11): the selected entry index is CURSOR + SCROLL, the window shows 5
// rows (cursor maxes at 4). After compacting a sold cell out we MUST clamp both,
// or the selection can sit past the new end — a "ghost" row backed by stale entry
// data that would otherwise still be confirmable.
static const uint32_t SHOP_CURSOR_ADDR  = 0xDD6B84;   // cursor row within the window
static const uint32_t SHOP_SCROLL_ADDR  = 0xDD6B94;   // index of the top visible row
static const int      SHOP_VISIBLE_ROWS = 5;

// A SHOP MUST NEVER REACH ZERO ENTRIES.
//
// Vanilla FF7 ships no empty shop, so its shop code never defends against one:
// with itemCount 0 the grid draws nothing, but the cursor/scroll keep whatever
// values they held and confirm still reads entries[cursor] -- stale bytes the
// player can "buy" as a ghost item. Reported 2026-09-02 at shop_slots_per_shop
// = 10, where Gold Saucer evicts ALL vanilla stock to fit ten AP slots and the
// shop therefore drains to nothing once every check in it has been bought.
//
// So compaction keeps a floor of one entry: the LAST sold AP cell is replaced
// by a real, cheap vanilla item of the same kind rather than removed. The shop
// stays a normal one-item shop the engine can render, scroll and sell from.
// (Same safe defaults Gold Saucer falls back to when a tiered pool comes up
// empty -- see ShopRandomizer::pickTiered.)
static const uint16_t FILLER_ITEM    = 0x00;   // Potion
static const uint16_t FILLER_MATERIA = 0x35;   // Restore

static void LoadSold() {
    g_soldSlots.clear();
    if (g_soldPath.empty()) return;
    std::ifstream f(g_soldPath);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t c1 = line.find(':'); if (c1 == std::string::npos) continue;
        size_t c2 = line.find(':', c1 + 1); if (c2 == std::string::npos) continue;
        uint32_t shop    = strtoul(line.substr(0, c1).c_str(), nullptr, 0);
        uint32_t section = strtoul(line.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 0);
        uint32_t index   = strtoul(line.substr(c2 + 1).c_str(), nullptr, 0);
        g_soldSlots.insert(SlotKey(shop, section, index));
    }
}

// Keep the shop selection inside the CURRENT stock. FF7 computes its scroll limit
// from the list length when the screen is built and does NOT re-clamp when the
// list shrinks under it, so after an in-visit removal the player could scroll back
// past the end onto a stale row (never redrawn, so it shows old pixels). Called
// every render frame while the shop is open: a no-op in range, so it never fights
// normal scrolling — it only blocks going past the real end.
static void ClampShopSelection(uint32_t shop) {
    if (shop >= 80) return;
    __try {
        volatile uint8_t* base = reinterpret_cast<volatile uint8_t*>(SHOP_TABLE + shop * 0x54);
        const int count = base[2];
        if (count > 10) return;                 // not a shop record we understand
        volatile uint32_t* pCursor = reinterpret_cast<volatile uint32_t*>(SHOP_CURSOR_ADDR);
        volatile uint32_t* pScroll = reinterpret_cast<volatile uint32_t*>(SHOP_SCROLL_ADDR);
        // An empty shop should now be impossible (RemoveSoldFromShop keeps a
        // floor of one entry), but this used to bail out at count <= 0 and leave
        // the selection wherever it was -- which is how the ghost row became
        // reachable in the first place. Pin it to the top rather than trust the
        // invariant.
        if (count <= 0) { *pCursor = 0; *pScroll = 0; return; }
        int cursor = static_cast<int>(*pCursor);
        int scroll = static_cast<int>(*pScroll);
        int maxScroll = count - SHOP_VISIBLE_ROWS; if (maxScroll < 0) maxScroll = 0;
        if (scroll > maxScroll) scroll = maxScroll;
        if (scroll < 0) scroll = 0;
        int maxCursor = count - scroll - 1;
        if (maxCursor > SHOP_VISIBLE_ROWS - 1) maxCursor = SHOP_VISIBLE_ROWS - 1;
        if (maxCursor < 0) maxCursor = 0;
        if (cursor > maxCursor) cursor = maxCursor;
        if (cursor < 0) cursor = 0;
        if (static_cast<int>(*pCursor) != cursor) *pCursor = static_cast<uint32_t>(cursor);
        if (static_cast<int>(*pScroll) != scroll) *pScroll = static_cast<uint32_t>(scroll);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Compact every already-sold AP cell out of the given shop's stock. Idempotent:
// re-scans after each removal (indices shift), and a cell already gone is skipped.
static void RemoveSoldFromShop(uint32_t shop) {
    if (shop >= 80) return;
    __try {
        volatile uint8_t* base = reinterpret_cast<volatile uint8_t*>(SHOP_TABLE + shop * 0x54);
        bool changedAny = false;
        for (;;) {
            const int count = base[2];
            if (count <= 0 || count > 10) break;

            // First cell that is one of our AP slots AND already bought.
            int      found   = -1;
            uint32_t section = KTEXT_ITEM;
            uint16_t idx     = 0;
            for (int n = 0; n < count; ++n) {
                volatile uint8_t* e = base + 4 + n * 8;
                uint32_t etype = *reinterpret_cast<volatile uint32_t*>(const_cast<uint8_t*>(e));
                uint16_t eidx  = *reinterpret_cast<volatile uint16_t*>(const_cast<uint8_t*>(e + 4));
                uint32_t esec  = (etype == 1) ? KTEXT_MATERIA : KTEXT_ITEM;
                uint64_t key   = SlotKey(shop, esec, eidx);
                if (g_apSlots.count(key) && g_soldSlots.count(key)) {
                    found = n; section = esec; idx = eidx;
                    break;
                }
            }
            if (found < 0) break;

            if (count == 1) {
                // Removing this one would empty the shop. Restock a plain vanilla
                // item of the same kind instead -- see FILLER_ITEM above.
                const bool     materia = (section == KTEXT_MATERIA);
                const uint16_t fill    = materia ? FILLER_MATERIA : FILLER_ITEM;
                // If the filler were itself an AP cell in THIS shop we would be
                // handing the player a second copy of a check, so leave the sold
                // cell in place instead (buyable, but the grant stays suppressed:
                // no item, no duplicate check, just wasted gil). Cannot happen
                // with the token ids Gold Saucer reserves; cheap to be sure.
                if (g_apSlots.count(SlotKey(shop, section, fill)))
                    break;
                volatile uint8_t* e = base + 4;
                e[0] = materia ? 1 : 0; e[1] = 0; e[2] = 0; e[3] = 0;
                e[4] = static_cast<uint8_t>(fill & 0xFF);
                e[5] = static_cast<uint8_t>((fill >> 8) & 0xFF);
                e[6] = 0; e[7] = 0;
                base[2] = 1;
                base[3] = 0;
                LogLine("last AP slot shop %u %u:%u sold -> restocked %u:%u "
                        "(a shop must never be empty)\n",
                        shop, section, idx, section, fill);
                changedAny = true;
                break;
            }

            for (int k = found; k < count - 1; ++k) {
                volatile uint8_t* dst = base + 4 + k * 8;
                volatile uint8_t* src = base + 4 + (k + 1) * 8;
                for (int b = 0; b < 8; ++b) dst[b] = src[b];
            }
            base[2] = static_cast<uint8_t>(count - 1);
            base[3] = 0;   // FF7 reads itemCount as a word; keep the pad byte 0
            LogLine("removed sold AP slot shop %u %u:%u (stock %d->%d)\n",
                    shop, section, idx, count, count - 1);
            changedAny = true;
        }
        // Blank EVERY vacated cell, not just the first past the new end: a visit
        // that compacts several slots leaves stale copies all the way up to the
        // old count, and those are what a ghost row reads from.
        if (changedAny) {
            int count = base[2];
            if (count < 0)  count = 0;
            if (count > 10) count = 10;
            for (int n = count; n < 10; ++n) {
                volatile uint8_t* tail = base + 4 + n * 8;
                for (int b = 0; b < 8; ++b) tail[b] = 0xFF;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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
        std::string value = line.substr(eq + 1);
        while (!value.empty() && (value.back()=='\r' || value.back()=='\n')) value.pop_back();
        // Optional "<name>|<description>" payload. Split on the FIRST '|'.
        std::string name = value, desc;
        size_t bar = value.find('|');
        if (bar != std::string::npos) {
            name = value.substr(0, bar);
            desc = value.substr(bar + 1);
        }
        // Key = "<shop>:<section>:<index>".
        size_t c1 = key.find(':');
        if (c1 == std::string::npos) continue;
        size_t c2 = key.find(':', c1 + 1);
        if (c2 == std::string::npos) continue;
        uint32_t shop    = strtoul(key.substr(0, c1).c_str(), nullptr, 0);
        uint32_t section = strtoul(key.substr(c1 + 1, c2 - c1 - 1).c_str(), nullptr, 0);
        uint32_t index   = strtoul(key.substr(c2 + 1).c_str(), nullptr, 0);
        const uint64_t k = SlotKey(shop, section, index);
        g_names[k] = EncodeFF7(name);
        if (!desc.empty()) g_descs[k] = EncodeFF7(desc);
        g_apSlots.insert(k);
    }
    LogLine("loaded %zu name + %zu description override(s); %zu AP shop slot(s)\n",
            g_names.size(), g_descs.size(), g_apSlots.size());
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

// Shop-open setup (FF7 0x719D7A): runs exactly once when a shop is entered, with
// the shop id as its stack arg, and writes it to 0xDD4724. We run the original,
// then reload the sold list and compact already-obtained AP cells out of THIS
// shop's stock — before the grid draws — so they can't be re-bought.
using ShopOpen_t = void(__cdecl*)(uint32_t);
static const uint32_t ADDR_SHOP_OPEN = 0x719D7A;
static ShopOpen_t oShopOpen = nullptr;
static void __cdecl hkShopOpen(uint32_t shopId) {
    if (oShopOpen) oShopOpen(shopId);
    LoadSold();
    RemoveSoldFromShop(shopId);
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
static uint32_t g_pendingShop    = 0;             // shop id captured when the pending check armed
static uint32_t g_pendingGil     = 0;             // gil baseline when the pending check armed
static DWORD    g_pendingTick    = 0;             // GetTickCount() when armed
static const DWORD kPendingWindowMs = 2500;       // hover→buy must drop gil within this

static void SignalPurchase(uint32_t shop, uint32_t section, uint32_t index);   // fwd decl

static char* __cdecl hkGetKernelText(uint32_t section, uint32_t index, uint32_t a3) {
    if (InMenu()) {
        // Drain a pending mid-visit removal between transactions (a slot bought
        // this frame vanishes from the grid on the next redraw).
        if (g_removePending) {
            g_removePending = false;
            RemoveSoldFromShop(g_removeShop);
        }
        // Keep the selection inside the live stock every frame — FF7 won't
        // re-clamp its own scroll after we shorten the list, so without this the
        // player can scroll back onto a stale (never-redrawn) row.
        if (g_inShop) ClampShopSelection(CurrentShopId());
        uint32_t g;
        if (ReadGil(g)) {
            // Resolve a pending materia check the moment gil drops below the armed
            // baseline (pay-after-grant); expire it if no drop within the window.
            if (g_pendingToken >= 0) {
                if (g < g_pendingGil) {
                    SignalPurchase(g_pendingShop, KTEXT_MATERIA, static_cast<uint32_t>(g_pendingToken));
                    g_soldSlots.insert(SlotKey(g_pendingShop, KTEXT_MATERIA,
                                               static_cast<uint32_t>(g_pendingToken)));
                    g_removeShop = g_pendingShop; g_removePending = true;
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
        const uint64_t k = SlotKey(CurrentShopId(), section, index);
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

static void SignalPurchase(uint32_t shop, uint32_t section, uint32_t index) {
    if (g_buysPath.empty()) return;
    // De-dup: one materia buy can be detected twice in the same instant — once by
    // the deferred render-sampler (gil-drop observed) and once by the immediate
    // grant-call path. Both map to the same location, so collapse repeats of the
    // same (shop,section,index) within a short window into a single signal.
    static uint64_t s_lastKey  = 0xFFFFFFFFFFFFFFFFull;
    static DWORD    s_lastTick  = 0;
    const uint64_t key = SlotKey(shop, section, index);
    const DWORD now = GetTickCount();
    if (key == s_lastKey && now - s_lastTick < 1500) return;
    s_lastKey = key; s_lastTick = now;
    FILE* fp = fopen(g_buysPath.c_str(), "a");
    if (!fp) { LogLine("shop_buys.txt append failed\n"); return; }
    fprintf(fp, "%u:%u:%u\n", shop, section, index);
    fclose(fp);
    LogLine("AP shop purchase signalled: %u:%u:%u\n", shop, section, index);
}

// AddItems(DWORD word) where word = (qty<<9) | item_id. __cdecl per the FF7 ABI
// for this menu routine; confirmed via logging before suppression is enabled.
using AddItem_t = void(__cdecl*)(uint32_t);
static AddItem_t oAddItem = nullptr;

static void __cdecl hkAddItem(uint32_t word) {
    const uint32_t id = word & 0x1FF;
    // Gate on g_inShop (the shop screen), NOT InMenu(): an item-space token id is a
    // real weapon/armor/item id the player can own and equip. Unequipping a weapon
    // whose id happens to be a token calls AddItems in the EQUIP menu (g_inShop
    // false) -> we pass it through so the item returns to inventory and no false
    // check fires. Only a real buy of the reserved AP cell (this shop + this id)
    // suppresses + signals.
    if (g_inShop) {
        const uint32_t shop = CurrentShopId();
        const uint64_t key = SlotKey(shop, KTEXT_ITEM, id);
        if (g_apSlots.count(key)) {
            SignalPurchase(shop, KTEXT_ITEM, id);   // suppress the grant entirely
            g_soldSlots.insert(key);                // remove from stock next frame
            g_removeShop = shop; g_removePending = true;
            return;
        }
    }
    if (oAddItem) oAddItem(word);
}

// add-materia routine: takes the materia id (low byte). __cdecl; confirmed via log.
using AddMateria_t = void(__cdecl*)(uint32_t);
static AddMateria_t oAddMateria = nullptr;

static void __cdecl hkAddMateria(uint32_t mid) {
    const uint32_t id = mid & 0xFF;
    // Gate on g_inShop + the reserved AP cell for THIS shop (a materia token id can
    // be a real materia the player owns, or vanilla stock in another shop — only
    // the exact reserved cell is an AP slot). The grant routine is also reached on
    // HOVER, so distinguish a buy by gil drop (a buy decreases gil, a hover doesn't).
    if (g_inShop) {
        const uint32_t shop = CurrentShopId();
        if (g_apSlots.count(SlotKey(shop, KTEXT_MATERIA, id))) {
            uint32_t gil = 0;
            const bool have = ReadGil(gil);
            const bool immediate = have && g_lastGil != kGilSentinel && gil < g_lastGil;
            if (immediate) {
                LogLine("addMateria shop %u token %u: gil %u < lastGil %u -> BUY\n",
                        shop, id, gil, g_lastGil);
                g_pendingToken = -1;
                SignalPurchase(shop, KTEXT_MATERIA, id);
                g_soldSlots.insert(SlotKey(shop, KTEXT_MATERIA, id));
                g_removeShop = shop; g_removePending = true;
            } else if (have) {
                // pay-after-grant / first sight: arm a deferred check; the render
                // sampler fires it if gil drops in the window, else treats it as hover.
                g_pendingToken = static_cast<int>(id);
                g_pendingShop  = shop;
                g_pendingGil   = gil;
                g_pendingTick  = GetTickCount();
                LogLine("addMateria shop %u token %u: gil %u -> armed pending\n", shop, id, gil);
            }
            return;   // ALWAYS suppress the reserved token grant (never enters inventory)
        }
        // NOT the AP cell. If a deferred check is still armed, the player has moved
        // OFF the AP slot onto real stock — disarm it.
        //
        // Without this, hovering the AP cell armed a pending token and nothing ever
        // cleared it except a gil drop, a 2.5s timeout, or leaving the menu. Moving
        // down and buying a REAL materia within that window dropped gil, and the
        // render sampler cashed in the ARMED token: the player got the AP check for
        // a Restore they bought themselves (reported 2026-07-27, Sector 5 materia).
        // The grant routine is called for whatever slot is under the cursor, so a
        // call with a different id is exactly the signal that the cursor moved.
        if (g_pendingToken >= 0 && g_pendingToken != static_cast<int>(id)) {
            LogLine("addMateria shop %u id %u is not an AP cell -> disarm pending token %d\n",
                    shop, id, g_pendingToken);
            g_pendingToken = -1;
        }
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
    g_soldPath = dir + "shop_sold.txt";

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

    // Shop-open setup: remove already-obtained AP cells from stock on entry so
    // they can't be re-bought. Only if there are AP slots to manage.
    if (!g_apSlots.empty() && LooksLikeCode(ADDR_SHOP_OPEN) &&
        MH_CreateHook(reinterpret_cast<void*>(ADDR_SHOP_OPEN), &hkShopOpen,
                      reinterpret_cast<void**>(&oShopOpen)) == MH_OK) {
        LogLine("shop-open(0x%X) hooked — sold AP slots removed from stock\n", ADDR_SHOP_OPEN);
    } else if (!g_apSlots.empty()) {
        LogLine("WARN: could not hook shop-open(0x%X) — sold slots stay buyable\n", ADDR_SHOP_OPEN);
    }

    // Suppress AP-token grants so purchased tokens never enter inventory. Both
    // grant routines are hooked whenever any AP slot exists; the hooks themselves
    // are scoped to (g_inShop && the reserved cell), so a non-AP add passes through.
    const bool haveSlots = !g_apSlots.empty();
    if (haveSlots && LooksLikeCode(ADDR_ADD_ITEM) &&
        MH_CreateHook(reinterpret_cast<void*>(ADDR_ADD_ITEM), &hkAddItem,
                      reinterpret_cast<void**>(&oAddItem)) == MH_OK) {
        LogLine("AddItems(0x%X) hooked — item-token grants suppressed\n", ADDR_ADD_ITEM);
    } else if (haveSlots) {
        LogLine("WARN: could not hook AddItems(0x%X) — item tokens NOT suppressed\n", ADDR_ADD_ITEM);
    }
    if (haveSlots && LooksLikeCode(ADDR_ADD_MATERIA) &&
        MH_CreateHook(reinterpret_cast<void*>(ADDR_ADD_MATERIA), &hkAddMateria,
                      reinterpret_cast<void**>(&oAddMateria)) == MH_OK) {
        LogLine("AddMateria(0x%X) hooked — materia-token grants suppressed\n", ADDR_ADD_MATERIA);
    } else if (haveSlots) {
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
