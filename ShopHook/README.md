# shophook.dll — native-grid Tier-3 AP shops

Makes the **real FF7 shop UI** sell Archipelago items: each slot shows the AP
item's per-owner name (e.g. *"A Potion @ Bnuy"*) and description
(*"An Archipelago Item for Bnuy"*), and **buying it fires an AP check without the
token ever entering the player's inventory** (the player still pays gil). Works
under **7th Heaven** because the AP client *injects* this DLL into the running
`FF7_EN.exe` (FFNx never loads a gameplay DLL as a mod — that's why a plain
MinHook DLL "wouldn't load"; injection sidesteps that entirely).

## Architecture

```
7th Heaven launches FF7 (FFNx)
  → AP client writes shop_ap.txt, then attaches (pymem) and injects shophook.dll
      → MinHook hooks 3 FF7 functions:
          1. get_kernel_text(section, idx, a3)  → AP name (a3=8) / description (a3=0)
          2. AddItems(0x6CBFFA)                 → reserved item-token grant SUPPRESSED
          3. add-materia(0x6CBCF3)              → reserved materia-token grant SUPPRESSED
      → on a suppressed buy, appends "<section>:<index>" to shop_buys.txt
  → AP client consumes shop_buys.txt each tick → fires the matching location check
Gold Saucer (7th Heaven mod) fills shop *contents* (Hext, reserved token ids)
```

Responsibility split: **DLL** = display override + purchase suppression/signalling
(runtime); **Gold Saucer/Hext** = which token ids each shop sells; **AP
client/world** = slot→item/owner/location mapping, writes `shop_ap.txt`, consumes
`shop_buys.txt`.

## Hooked FF7 functions (ff7_en.exe, Steam US)

All documented (Qhimm "Custom Game Settings" RE thread + FFNx externals) and in
the stable `0x6CB…` range — **no Cheat Engine / breakpoints needed** (they crash
this build). Confirm live via `shophook_log.txt`; resolution is guarded by
`__try/__except` + a prologue sanity check so an unexpected exe fails safe.

| Hook | VA | Notes |
|---|---|---|
| `get_kernel_text` | resolved via FFNx-style rel-call chain | a3=8 name, a3=0 description; gated to Menu module (current_module==5); also samples party gil each render frame |
| `AddItems` | `0x6CBFFA` | arg = `(qty<<9) | item_id`; suppressed for reserved item tokens |
| add-materia | `0x6CBCF3` | arg = materia id; suppressed for reserved materia tokens **only when gil just dropped** (see below) |

Gil is deducted by a **separate** `DecreaseGil` call, so suppressing only the
grant leaves the player paying for the slot.

### Materia hover guard (gil-drop gate)

The materia shop shares code with the equip/materia menu, so `0x6CBCF3` is also
reached while merely **hovering** a materia — which must NOT fire a check. Party
gil (savemap `+0x0B7C`, the client's `GIL_OFFSET`) is sampled every render frame
inside `hkGetKernelText`; `hkAddMateria` only signals + suppresses when the
current gil is **below** that last-frame value (a real purchase spends gil; a
hover doesn't). Items don't need this — the item shop's grant routine only runs on
an actual buy.

## shop_ap.txt (written by the AP client, read by the DLL at load)

One line per AP shop slot:

```
<section>:<index>=<name>[|<description>]
```

- `section` 4 = carried items (composite id), 13 = materia.
- `<name>` overrides the slot name (a3=8); optional `|<description>` overrides the
  info-pane text (a3=0). Legacy `<itemId>=<name>` lines are still accepted
  (treated as section 4, no description).
- Each reserved `index` is also tracked as a token to suppress on purchase.

## shop_buys.txt (written by the DLL, consumed by the AP client)

Appended one line per suppressed purchase, `<section>:<index>`. The client maps it
to the slot's AP location, fires the check, then truncates the file (consume-once).

## Build & install

```
cmake -B build -A Win32          # MUST be 32-bit — FF7_EN.exe is x86
cmake --build build --config Release
copy build\Release\shophook.dll  "<FF7 install>\shophook.dll"   # next to FF7_EN.exe
```
The AP client auto-injects `shophook.dll` if it sits beside `FF7_EN.exe`.

## Verifying / debugging

1. First build can run **log-only** (comment out the two `MH_CreateHook` calls for
   the add routines) to confirm `AddItems(0x6CBFFA)` fires with the expected token
   word in `shophook_log.txt`, then enable suppression.
2. On a token buy, check `shophook_log.txt` shows `AP shop purchase signalled` and
   that party gil dropped (gil is the separate `DecreaseGil` call). If gil does NOT
   drop, the buy code grants gil after the add — have the DLL deduct the slot price
   in `hkAddItem`.
3. The `a3` argument for the description call is assumed to be `0` (per the file
   header). If descriptions don't override, log `a3` values in `hkGetKernelText`
   while browsing a shop and adjust `KTEXT_A3_DESC`.
4. If a **materia** purchase stops firing (vs. hover false-firing before the fix),
   gil is being deducted *after* the grant for that path — switch the gate to arm
   on a hooked `DecreaseGil` instead of the per-frame gil sample.
