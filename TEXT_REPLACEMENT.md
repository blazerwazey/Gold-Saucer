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

## Fixes after the first in-game test

Reported: windows too small for some items, and wrong names.

**Windows.** The resize only ran for multi-line text. A *single*-line AP message
is often wider than the vanilla one it replaces — `Sent "Vivian" to FoomTTYD!`
is 26 columns and needs ~191px, but inherited the window vanilla sized for
`Received "Potion"!` (18 columns, ~146px) and was clipped on the right. It now
runs for every replacement; height is untouched for a single line, so it only
adds the width the text needs. The width formula itself was fine — measured
across vanilla, 6.9px per character against the 7.0 assumed.

Also: the WINDOW search was capped at 300 bytes, which missed 40 of 158
pickups whose WINDOW sits further back. It now scans the whole script region.
The remaining ~23% have no WINDOW opcode at all, and that is fine — vanilla
ships 4,453 such messages, **69% of them multi-line** (some 5+), so the game
auto-sizes when no window is set.

**Names.** Nearest-MESSAGE cross-assigned inside chest clusters: in `blin62_1`
the three source pickups took Elixir / Ether / Potion — each a *neighbour's*
message. Worse, most MESSAGEs near a pickup are ordinary dialogue, so the rule
would overwrite an NPC's line with `Sent "X" to Bob!`.

The vanilla text names the vanilla item (`Received "Ether"!`), so that is now
the signal: a candidate that names the item being replaced wins outright,
proximity is only the tie-break, and **if nothing names it the pickup is left
vanilla** rather than guessing. Matching is punctuation/case-insensitive with a
one-character tolerance, because the game's spelling and the item table
disagree here and there ("Four Slot"/"Four Slots", "Glow Lance"/"Grow Lance").

Measured over vanilla flevel, same pickups, both rules:

| | nearest-MESSAGE | names-the-item |
|---|---|---|
| correct | 127 | **154** |
| wrong message hijacked | **389** | 0 |
| skipped, keeps vanilla text | 0 | 371 |

More correct assignments *and* no hijacking — looking past the nearest candidate
finds messages proximity was missing. (That 516 total comes from a deliberately
loose STITM scan, looser than `scanForSTITM`, so many "skipped" are not real
pickups.) The standalone randomizer keeps its old proximity behaviour; only the
AP path refuses to guess.

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

## Key items

Key items take a different route in AP mode: `replaceVanillaBitonsForAP()`
rewrites the *vanilla key-item BITON* in place rather than replacing an STITM,
so it needed its own plumbing. It now composes the same message and appends an
`OpcodeModification` alongside the BITON rewrite, keyed on `keyItemName`.

Their vanilla text has its own shape — `Received Key Item "Keycard 62"!` — which
the names-the-item rule reads just as well. Measured over vanilla flevel:

| | sites |
|---|---|
| gets AP text | **35** |
| skipped, keeps vanilla | 8 |

The 8 grant their item through dialogue with nothing naming it nearby
(`blin59`'s Keycard 60 sits next to "Destroy the intruders!").

Matching also tolerates the game dropping a parenthetical qualifier: `ncorel3`
says `Received Key Item "Huge Materia"!` where the table says "Huge Materia
(Corel)". That alone moved key-item coverage from 29 to 35. Where one field
holds two such items (`rcktin4` has Corel and Fort Condor) the used-message
guard stops both claiming the same text; the second is skipped, not mislabelled.

Shared and sibling BITONs each get their own entry — they are separate code
paths in the field, each with its own message — and neutralized BITONs (no AP
placement) get no text, which is correct: nothing was placed there.

## Not done yet
- Shop text, and the `Config::TextReplacement` feature flag (currently unused by
  this path — the AP text is unconditional in AP mode).
- The old `TextReplacementManager` is a **different, unwired** subsystem (kernel
  item renaming for the starting-equipment randomizer). Untouched here.
