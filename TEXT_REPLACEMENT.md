# Archipelago field text replacement

Fork of Gold Saucer (`upstream` = blazerwazey/Gold-Saucer), branch
`text-replacement`. Goal: a chest's message should name the item Archipelago
actually placed there, not the vanilla one it replaced.

## What was already there

`updateFieldTexts()` (FieldPickupRandomizer_ff7tk.cpp) was already complete and
working — it finds the MESSAGE nearest a patched pickup, appends new entries to
the field's text section, and fixes the section-0 size header, the AKAO position
table and section offsets 1–8. **It only ran for standalone randomization.**

In AP mode `applySTITMAsArchipelago()` / `applySMTRAAsArchipelago()` rewrote the
opcode to a BITON and never appended an `OpcodeModification`, so no text was
touched. Everything needed was already in the seed — `item`, `item_owner`,
`item_is_local` — and `loadApJson()` parsed the placement and discarded all
three.

## What this branch adds

1. **`ApBitonCoord` carries the placement's display info** (`apItem`, `apOwner`,
   `apLocal`). It rides on the coord rather than a parallel table because the
   coord *is* the placement: the queue and the last-BITON fallback both hand
   back the right one for free.
2. **`composeApPickupText()`** builds the sentence and FF7-encodes it:
   - local: `Received "Hi-Potion"!`
   - remote: `Sent "Rocket Launcher" to Bob!`
   - greedy word wrap at 26 columns, up to 3 lines, joined with `0xE7`
     (the newline byte, same one the crater welcome banner uses).
3. **`OpcodeModification::encodedText`** — when set, `updateFieldTexts` writes it
   verbatim instead of composing `Received "<newName>"!`. AP needs this because
   the sentence depends on the receiving player, and the AP item has no relation
   to the vanilla opcode (a materia chest can hold another player's weapon, so
   the item/materia distinction the vanilla path infers is simply wrong).
4. **`resizeMessageWindow()`** — grows the vanilla WINDOW so multi-line text
   fits. FF7 does not wrap or auto-grow a scripted window; text past the frame
   is not drawn, and vanilla pickup windows were sized for one short line.

### Where the window numbers come from

Measured across **all 10,107 WINDOW/MESSAGE pairs in vanilla flevel**:

| lines | n | median height | median width |
|---|---|---|---|
| 1 | 1517 | 25 | 154 |
| 2 | 3900 | 41 | 174 |
| 3 | 2973 | 57 | 209 |
| 4 | 1121 | 73 | 227 |

Height is exactly `16 * lines + 9`. Width tracks the longest line at roughly
7px per character plus frame. The resize only ever **grows** a window, clamps to
320x240, and nudges x/y back if the wider box would run off screen.

## Verified

- Configures and builds clean from a fresh CMake configure.
- Composition checked against **two real seeds, 365 placements**: 0 truncated,
  0 over-width lines, at most 2 lines each. One seed is 123/193 remote items, so
  the remote phrasing is the common case, not an edge case.

## NOT verified

- **Nothing has been run in game.** The tool has no headless mode, so producing
  a flevel and looking at a chest needs a manual run.
- The resize picks the nearest preceding `0x50` with a matching window id. `0x50`
  also occurs as operand data; the field values are range-checked (w/h non-zero
  and on-screen), but a false match would move the wrong window. The debug log
  prints every resize (`AP_WINDOW @off id=N WxH -> WxH`) — check it on the first
  real run.

## Not done yet

- **Key items in AP mode.** `replaceVanillaBitonsForAP()` is a separate path and
  was not plumbed, so key-item pickups keep their vanilla text.
- Shop text, and the `Config::TextReplacement` feature flag (currently unused by
  this path — the AP text is unconditional in AP mode).
- The old `TextReplacementManager` is a **different, unwired** subsystem (kernel
  item renaming for the starting-equipment randomizer). Untouched here.
