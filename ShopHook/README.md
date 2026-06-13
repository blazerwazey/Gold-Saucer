# shophook.dll — native-grid Tier-3 AP shops

Makes the **real FF7 shop UI** sell Archipelago items: each slot shows the AP
item's per-owner name (e.g. *"Bnuy's Potion"*) and **buying it fires an AP
check**. Works under **7th Heaven** because the AP client *injects* this DLL into
the running `FF7_EN.exe` (FFNx never loads a gameplay DLL as a mod — that's why a
plain MinHook DLL "wouldn't load"; injection sidesteps that entirely).

## Architecture

```
7th Heaven launches FF7 (FFNx)
  → AP client attaches (pymem) → injects shophook.dll  (worlds/ff7/dll_inject.py)
      → MinHook hooks 2 FF7 functions:
          1. item-name lookup  → returns the AP slot's display name (Tier 3)
          2. shop "buy"        → sets a savemap detection bit
  → AP client polls that bit (like a field pickup) → fires the check
Gold Saucer (7th Heaven mod) fills shop *contents* (Hext) + writes shop_ap.json
```

Responsibility split: **DLL** = display + purchase-flagging (runtime); **Gold
Saucer/Hext** = which items each shop sells + `shop_ap.json`; **AP world** =
defines shop-slot *locations*, allocates their flag bits, and is the source of
truth for slot→item/owner/name.

## What's done vs. what you must supply

Done: injection (client), MinHook bootstrap, config-driven flag write, build.
**Missing = exactly two FF7 addresses + their signatures** (fill the `TODO`s in
`dllmain.cpp`):

| Hook | What to find | How |
|---|---|---|
| `ADDR_GET_ITEM_NAME` | the routine the **shop menu** calls to get an item's display name | In Cheat Engine, open a shop and breakpoint the code that reads the kernel item-name text while the grid draws; or check **FFNx** source (`ff7.h`/menu) for a named address. |
| `ADDR_SHOP_BUY` | the routine that runs when you **confirm a purchase** (gil deducted, item granted) | Breakpoint on the gil write while buying; walk up the call stack to the shop-buy function. Its args expose the current **shop id + slot**. |

Reference data already known (from Gold Saucer `ShopRandomizer`):
- **Shop table**: 80 shops at **file offset `0x521E18`**, `84` bytes each
  (`type:u16, count:u8, pad:u8, 10×{type:u32, index:u16, pad:u16}`). Item index
  encoding: items `0x00–0x68`, weapons `0x80+`, armor `0x100+`, accessory
  `0x120+`. Convert file offset → runtime VA via the `.exe` section map (base
  `0x400000`) when you need the in-memory table.
- **Savemap base** in memory: `0xDBFD38` (same as the AP client).

## Detection flag region (AP side)

Shop slots have no natural flag, so allocate a **dedicated clean savemap region**
for shop detection bits — a block no field/minigame script writes. Shops are far
fewer than the ~380 field items (~80 shops × ≤10 slots), so a small reserved
range is plenty and avoids the Fort-Condor-style collision. The AP world assigns
each shop-slot location a `(flag_off, flag_bit)` here and writes them into
`shop_ap.json`; the DLL ORs the bit on purchase; the client polls it.

## shop_ap.json (written by AP side, read by the DLL)

```json
{ "<shopId>:<slotIndex>": { "name": "Bnuy's Potion", "flag_off": 4000, "flag_bit": 0 } }
```

## Build & install

```
cmake -B build -A Win32          # MUST be 32-bit — FF7_EN.exe is x86
cmake --build build --config Release
copy build\Release\shophook.dll  "<FF7 install>\shophook.dll"   # next to FF7_EN.exe
```
The AP client auto-injects `shophook.dll` if it sits beside `FF7_EN.exe`.

## Remaining AP-side work (after addresses are found)

1. Define shop-slot **locations** + access rules in the world (shops on
   unreachable maps excluded, like other inaccessible field locations).
2. Allocate the shop **flag region** + emit `(flag_off, flag_bit)` per slot.
3. Have Gold Saucer set shop **contents** (Hext) to the AP placements + write
   `shop_ap.json`.
4. Pricing: area-scaled, low early (per the design discussion).
