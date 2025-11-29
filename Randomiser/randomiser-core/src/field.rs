use crate::field_compiler::compile_script_from_str;
use crate::items::{lookup_inventory_name, lookup_materia_name};

pub(crate) fn is_field_materia_id_allowed(id: u8) -> bool {
    id < 0x80
}

// Fixed opcode lengths for FF7 field scripts (PC), mirroring Makou
// Reactor's Opcode::length[257] table. This is used together with
// opcode_size_pc to walk scripts opcode-by-opcode.
const OPCODE_LENGTH: [u8; 257] = [
    /* 00 RET      */ 1,
    /* 01 REQ      */ 3,
    /* 02 REQSW    */ 3,
    /* 03 REQEW    */ 3,
    /* 04 PREQ     */ 3,
    /* 05 PRQSW    */ 3,
    /* 06 PRQEW    */ 3,
    /* 07 RETTO    */ 2,
    /* 08 JOIN     */ 2,
    /* 09 SPLIT    */ 15,
    /* 0a SPTYE    */ 6,
    /* 0b GTPYE    */ 6,
    /* 0c          */ 1,
    /* 0d          */ 1,
    /* 0e DSKCG    */ 2,
    /* 0f SPECIAL  */ 2,

    /* 10 JMPF     */ 2,
    /* 11 JMPFL    */ 3,
    /* 12 JMPB     */ 2,
    /* 13 JMPBL    */ 3,
    /* 14 IFUB     */ 6,
    /* 15 IFUBL    */ 7,
    /* 16 IFSW     */ 8,
    /* 17 IFSWL    */ 9,
    /* 18 IFUW     */ 8,
    /* 19 IFUWL    */ 9,
    /* 1a          */ 10,
    /* 1b          */ 3,
    /* 1c          */ 6,
    /* 1d          */ 1,
    /* 1e          */ 1,
    /* 1f          */ 1,

    /* 20 MINIGAME */ 11,
    /* 21 TUTOR    */ 2,
    /* 22 BTMD2    */ 5,
    /* 23 BTRLD    */ 3,
    /* 24 WAIT     */ 3,
    /* 25 NFADE    */ 9,
    /* 26 BLINK    */ 2,
    /* 27 BGMOVIE  */ 2,
    /* 28 KAWAI    */ 3,
    /* 29 KAWIW    */ 1,
    /* 2a PMOVA    */ 2,
    /* 2b SLIP     */ 2,
    /* 2c BGPDH    */ 5,
    /* 2d BGSCR    */ 7,
    /* 2e WCLS     */ 2,
    /* 2f WSIZW    */ 10,

    /* 30 IFKEY    */ 4,
    /* 31 IFKEYON  */ 4,
    /* 32 IFKEYOFF */ 4,
    /* 33 UC       */ 2,
    /* 34 PDIRA    */ 2,
    /* 35 PTURA    */ 4,
    /* 36 WSPCL    */ 5,
    /* 37 WNUMB    */ 8,
    /* 38 STTIM    */ 6,
    /* 39 GOLDu    */ 6,
    /* 3a GOLDd    */ 6,
    /* 3b CHGLD    */ 4,
    /* 3c HMPMAX1  */ 1,
    /* 3d HMPMAX2  */ 1,
    /* 3e MHMMX    */ 1,
    /* 3f HMPMAX3  */ 1,

    /* 40 MESSAGE  */ 3,
    /* 41 MPARA    */ 5,
    /* 42 MPRA2    */ 6,
    /* 43 MPNAM    */ 2,
    /* 44          */ 1,
    /* 45 MPu      */ 5,
    /* 46          */ 1,
    /* 47 MPd      */ 5,
    /* 48 ASK      */ 7,
    /* 49 MENU     */ 4,
    /* 4a MENU2    */ 2,
    /* 4b BTLTB    */ 2,
    /* 4c          */ 1,
    /* 4d HPu      */ 5,
    /* 4e          */ 1,
    /* 4f HPd      */ 5,

    /* 50 WINDOW   */ 10,
    /* 51 WMOVE    */ 6,
    /* 52 WMODE    */ 4,
    /* 53 WREST    */ 2,
    /* 54 WCLSE    */ 2,
    /* 55 WROW     */ 3,
    /* 56 GWCOL    */ 7,
    /* 57 SWCOL    */ 7,
    /* 58 STITM    */ 5,
    /* 59 DLITM    */ 5,
    /* 5a CKITM    */ 5,
    /* 5b SMTRA    */ 7,
    /* 5c DMTRA    */ 8,
    /* 5d CMTRA    */ 10,
    /* 5e SHAKE    */ 8,
    /* 5f NOP      */ 1,

    /* 60 MAPJUMP  */ 10,
    /* 61 SCRLO    */ 2,
    /* 62 SCRLC    */ 5,
    /* 63 SCRLA    */ 6,
    /* 64 SCR2D    */ 6,
    /* 65 SCRCC    */ 1,
    /* 66 SCR2DC   */ 9,
    /* 67 SCRLW    */ 1,
    /* 68 SCR2DL   */ 9,
    /* 69 MPDSP    */ 2,
    /* 6a VWOFT    */ 7,
    /* 6b FADE     */ 9,
    /* 6c FADEW    */ 1,
    /* 6d IDLCK    */ 4,
    /* 6e LSTMP    */ 3,
    /* 6f SCRLP    */ 6,

    /* 70 BATTLE   */ 4,
    /* 71 BTLON    */ 2,
    /* 72 BTLMD    */ 3,
    /* 73 PGTDR    */ 4,
    /* 74 GETPC    */ 4,
    /* 75 PXYZI    */ 8,
    /* 76 PLUS!    */ 4,
    /* 77 PLUS2!   */ 5,
    /* 78 MINUS!   */ 4,
    /* 79 MINUS2!  */ 5,
    /* 7a INC!     */ 3,
    /* 7b INC2!    */ 3,
    /* 7c DEC!     */ 3,
    /* 7d DEC2!    */ 3,
    /* 7e TLKON    */ 2,
    /* 7f RDMSD    */ 3,

    /* 80 SETBYTE  */ 4,
    /* 81 SETWORD  */ 5,
    /* 82 BITON    */ 4,
    /* 83 BITOFF   */ 4,
    /* 84 BITXOR   */ 4,
    /* 85 PLUS     */ 4,
    /* 86 PLUS2    */ 5,
    /* 87 MINUS    */ 4,
    /* 88 MINUS2   */ 5,
    /* 89 MUL      */ 4,
    /* 8a MUL2     */ 5,
    /* 8b DIV      */ 4,
    /* 8c DIV2     */ 5,
    /* 8d MOD      */ 4,
    /* 8e MOD2     */ 5,
    /* 8f AND      */ 4,

    /* 90 AND2     */ 5,
    /* 91 OR       */ 4,
    /* 92 OR2      */ 5,
    /* 93 XOR      */ 4,
    /* 94 XOR2     */ 5,
    /* 95 INC      */ 3,
    /* 96 INC2     */ 3,
    /* 97 DEC      */ 3,
    /* 98 DEC2     */ 3,
    /* 99 RANDOM   */ 3,
    /* 9a LBYTE    */ 4,
    /* 9b HBYTE    */ 5,
    /* 9c 2BYTE    */ 6,
    /* 9d SETX     */ 7,
    /* 9e GETX     */ 7,
    /* 9f SEARCHX  */ 11,

    /* a0 PC       */ 2,
    /* a1 CHAR     */ 2,
    /* a2 DFANM    */ 3,
    /* a3 ANIME1   */ 3,
    /* a4 VISI     */ 2,
    /* a5 XYZI     */ 11,
    /* a6 XYI      */ 9,
    /* a7 XYZ      */ 9,
    /* a8 MOVE     */ 6,
    /* a9 CMOVE    */ 6,
    /* aa MOVA     */ 2,
    /* ab TURA     */ 4,
    /* ac ANIMW    */ 1,
    /* ad FMOVE    */ 6,
    /* ae ANIME2   */ 3,
    /* af ANIM!1   */ 3,

    /* b0 CANIM1   */ 5,
    /* b1 CANM!1   */ 5,
    /* b2 MSPED    */ 4,
    /* b3 DIR      */ 3,
    /* b4 TURNGEN  */ 6,
    /* b5 TURN     */ 6,
    /* b6 DIRA     */ 2,
    /* b7 GETDIR   */ 4,
    /* b8 GETAXY   */ 5,
    /* b9 GETAI    */ 4,
    /* ba ANIM!2   */ 3,
    /* bb CANIM2   */ 5,
    /* bc CANM!2   */ 5,
    /* bd ASPED    */ 4,
    /* be          */ 1,
    /* bf CC       */ 2,

    /* c0 JUMP     */ 11,
    /* c1 AXYZI    */ 8,
    /* c2 LADER    */ 15,
    /* c3 OFST     */ 12,
    /* c4 OFSTW    */ 1,
    /* c5 TALKR    */ 3,
    /* c6 SLIDR    */ 3,
    /* c7 SOLID    */ 2,
    /* c8 PRTYP    */ 2,
    /* c9 PRTYM    */ 2,
    /* ca PRTYE    */ 4,
    /* cb IFPRTYQ  */ 3,
    /* cc IFMEMBQ  */ 3,
    /* cd MMBud    */ 3,
    /* ce MMBLK    */ 2,
    /* cf MMBUK    */ 2,

    /* d0 LINE     */ 13,
    /* d1 LINON    */ 2,
    /* d2 MPJPO    */ 2,
    /* d3 SLINE    */ 16,
    /* d4 SIN      */ 10,
    /* d5 COS      */ 10,
    /* d6 TLKR2    */ 4,
    /* d7 SLDR2    */ 4,
    /* d8 PMJMP    */ 3,
    /* d9 PMJMP2   */ 1,
    /* da AKAO2    */ 15,
    /* db FCFIX    */ 2,
    /* dc CCANM    */ 4,
    /* dd ANIMB    */ 1,
    /* de TURNW    */ 1,
    /* df MPPAL    */ 11,

    /* e0 BGON     */ 4,
    /* e1 BGOFF    */ 4,
    /* e2 BGROL    */ 3,
    /* e3 BGROL2   */ 3,
    /* e4 BGCLR    */ 3,
    /* e5 STPAL    */ 5,
    /* e6 LDPAL    */ 5,
    /* e7 CPPAL    */ 5,
    /* e8 RTPAL    */ 7,
    /* e9 ADPAL    */ 10,
    /* ea MPPAL2   */ 10,
    /* eb STPLS    */ 5,
    /* ec LDPLS    */ 5,
    /* ed CPPAL2   */ 8,
    /* ee RTPAL2   */ 8,
    /* ef ADPAL2   */ 11,

    /* f0 MUSIC    */ 2,
    /* f1 SOUND    */ 5,
    /* f2 AKAO     */ 14,
    /* f3 MUSVT    */ 2,
    /* f4 MUSVM    */ 2,
    /* f5 MULCK    */ 2,
    /* f6 BMUSC    */ 2,
    /* f7 CHMPH    */ 4,
    /* f8 PMVIE    */ 2,
    /* f9 MOVIE    */ 1,
    /* fa MVIEF    */ 3,
    /* fb MVCAM    */ 2,
    /* fc FMUSC    */ 2,
    /* fd CMUSC    */ 8,
    /* fe CHMST    */ 3,
    /* ff GAMEOVER */ 1,
    /* 100 LABEL   */ 0,
];

pub(crate) fn opcode_size_pc(buf: &[u8], i: usize, end: usize) -> usize {
    if i >= end {
        return 0;
    }

    let op = buf[i] as usize;
    let mut size = if op < OPCODE_LENGTH.len() {
        OPCODE_LENGTH[op] as usize
    } else {
        1
    };

    match op {
        // Unused1C has an extra variable-length payload; subSize is stored
        // at offset +5 and is capped at 128 bytes in Makou's implementation.
        0x1C => {
            if i + 6 <= end {
                let sub = buf[i + 5].min(128);
                size = size.saturating_add(sub as usize);
            }
        }
        // KAWAI stores the full opcode size (including the opcode byte)
        // in opcodeSize at offset +1.
        0x28 => {
            if i + 2 <= end {
                let op_size = buf[i + 1] as usize;
                if op_size > 0 {
                    size = op_size.min(end - i);
                }
            }
        }
        _ => {}
    }

    if size == 0 || i + size > end {
        return std::cmp::min(1, end - i);
    }

    size
}

pub(crate) fn get_pc_field_script_range(buf: &[u8]) -> Option<(usize, usize)> {
    // Parse a PC field file header to locate the scripts/texts section
    // (Section1), then use the Section1 header's posTexts value to
    // approximate the end of the script bytecode region. This gives us a
    // conservative [start, end) range where opcodes like STITM (0x58)
    // should live, avoiding false positives in other sections or text.

    // PC field header: 2 bytes padding, 4-byte section count (9), then
    // 9 * 4-byte section positions. Each section is stored as
    // [u32 size][data]. Section 0 is Scripts (Section1).
    if buf.len() < 6 + 9 * 4 {
        return None;
    }

    let section_count = u32::from_le_bytes([buf[2], buf[3], buf[4], buf[5]]);
    if section_count != 9 {
        return None;
    }

    let mut section_positions = [0u32; 9];
    let mut o = 6usize;
    for i in 0..9 {
        section_positions[i] = u32::from_le_bytes([
            buf[o],
            buf[o + 1],
            buf[o + 2],
            buf[o + 3],
        ]);
        o += 4;
    }

    let s0 = section_positions[0] as usize;
    let s1 = section_positions[1] as usize;
    if s0 + 4 > buf.len() || s1 <= s0 + 4 || s1 > buf.len() {
        return None;
    }

    // Skip the 4-byte length header of Section1.
    let sec1_start = s0 + 4;
    let sec1_end = s1;
    let section1 = &buf[sec1_start..sec1_end];

    // Section1 header (non-demo PC):
    // 0: u16 version
    // 2: u8  nbScripts
    // 3: u8  model count
    // 4: u16 posTexts (and logical end of scripts)
    if section1.len() < 6 {
        return Some((sec1_start, sec1_end));
    }

    let pos_texts = u16::from_le_bytes([section1[4], section1[5]]) as usize;
    if pos_texts == 0 {
        return Some((sec1_start, sec1_end));
    }

    let script_end_rel = pos_texts.min(section1.len());
    let script_end = sec1_start + script_end_rel;
    Some((sec1_start, script_end))
}

pub(crate) fn get_pc_field_text_layout(buf: &[u8]) -> Option<(usize, u16, Vec<u16>)> {
    if buf.len() < 6 + 9 * 4 {
        return None;
    }

    let section_count = u32::from_le_bytes([buf[2], buf[3], buf[4], buf[5]]);
    if section_count != 9 {
        return None;
    }

    let mut section_positions = [0u32; 9];
    let mut o = 6usize;
    for i in 0..9 {
        section_positions[i] = u32::from_le_bytes([
            buf[o],
            buf[o + 1],
            buf[o + 2],
            buf[o + 3],
        ]);
        o += 4;
    }

    let s0 = section_positions[0] as usize;
    let s1 = section_positions[1] as usize;
    if s0 + 4 > buf.len() || s1 <= s0 + 4 || s1 > buf.len() {
        return None;
    }

    let sec1_start = s0 + 4;
    let sec1_end = s1;
    let section1 = &buf[sec1_start..sec1_end];

    if section1.len() < 6 {
        return None;
    }

    let pos_texts = u16::from_le_bytes([section1[4], section1[5]]) as usize;
    if pos_texts == 0 || pos_texts + 4 > section1.len() {
        return None;
    }

    let texts_base = sec1_start + pos_texts;
    if texts_base + 4 > buf.len() {
        return None;
    }

    let first_pos = u16::from_le_bytes([
        buf[texts_base + 2],
        buf[texts_base + 3],
    ]) as usize;
    if first_pos < 4 {
        return None;
    }

    let text_count = (first_pos / 2).saturating_sub(1) as u16;
    if text_count == 0 {
        return Some((texts_base, 0, Vec::new()));
    }

    let mut positions = Vec::with_capacity(text_count as usize);
    for i in 0..text_count {
        let base = texts_base + 2 + (i as usize) * 2;
        if base + 2 > buf.len() {
            return None;
        }
        let pos = u16::from_le_bytes([buf[base], buf[base + 1]]);
        positions.push(pos);
    }

    Some((texts_base, text_count, positions))
}

fn encode_ff7_ascii(s: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len());
    for ch in s.chars() {
        let c = if ch.is_ascii() { ch as u8 } else { b'?' };
        let code = if (0x20..=0x7E).contains(&c) {
            c.wrapping_sub(0x20)
        } else {
            b'?' - 0x20
        };
        out.push(code);
    }
    out
}

fn build_pickup_message_bytes(
    _qty: u8,
    item_id: u16,
    is_materia: bool,
    capacity: usize,
) -> Option<Vec<u8>> {
    // Try name-based variants from longest to shortest and pick the first
    // that fits in the available capacity.
    let name = if is_materia {
        let mid = (item_id & 0xFF) as u8;
        lookup_materia_name(mid)
    } else {
        lookup_inventory_name(item_id)
    };

    let name_variants = [
        format!("Found \"{}\"!", name),
        format!("Found {}!", name),
        name.to_string(),
    ];

    for s in &name_variants {
        let bytes = encode_ff7_ascii(s);
        if bytes.len() + 1 <= capacity {
            return Some(bytes);
        }
    }

    // Fallback to short generic variants by category if even the raw name
    // does not fit.
    let item_candidates = [
        "Found \"Randomized Item!\"",
        "Randomized Item!",
        "Rnd Item!",
        "Item!",
    ];

    let weapon_candidates = [
        "Found \"Randomized Weapon!\"",
        "Randomized Weapon!",
        "Rnd Weapon!",
        "Weapon!",
    ];

    let armor_candidates = [
        "Found \"Randomized Armor!\"",
        "Randomized Armor!",
        "Rnd Armor!",
        "Armor!",
    ];

    let accessory_candidates = [
        "Found \"Randomized Accessory!\"",
        "Randomized Accessory!",
        "Rnd Accessory!",
        "Accessory!",
    ];

    let candidates: &[_] = if item_id <= 0x0068 {
        &item_candidates
    } else if item_id >= 0x0080 && item_id < 0x0100 {
        &weapon_candidates
    } else if item_id >= 0x0100 && item_id < 0x0120 {
        &armor_candidates
    } else if item_id >= 0x0120 && item_id < 0x0140 {
        &accessory_candidates
    } else {
        &item_candidates
    };

    for s in candidates {
        let bytes = encode_ff7_ascii(s);
        if bytes.len() + 1 <= capacity {
            return Some(bytes);
        }
    }

    None
}

fn find_following_message(
    buf: &[u8],
    start: usize,
    script_end: usize,
) -> Option<(usize, u8)> {
    let mut i = start;
    let size = opcode_size_pc(buf, i, script_end);
    if size == 0 {
        return None;
    }
    i = i.saturating_add(size);

    while i < script_end {
        let op = buf[i];
        if op == 0x40 {
            let text_id = buf.get(i + 2).copied().unwrap_or(0);
            return Some((i, text_id));
        }
        if op == 0x00 {
            break;
        }
        let sz = opcode_size_pc(buf, i, script_end);
        if sz == 0 {
            break;
        }
        i = i.saturating_add(sz);
    }

    None
}

pub(crate) fn find_nearby_message(
    buf: &[u8],
    script_start: usize,
    script_end: usize,
    start: usize,
    max_distance: usize,
) -> Option<(usize, u8)> {
    // First, try the structured scan: walk opcodes forward to find a
    // MESSAGE after the opcode, then walk from the start of the script to
    // find the last MESSAGE before it. This mirrors the earlier behaviour
    // and works well for most fields.
    let mut best: Option<(usize, u8, usize)> = None; // (off, id, dist)

    if let Some((off, text_id)) =
        find_following_message(buf, start, script_end)
    {
        let dist = off.saturating_sub(start);
        if dist <= max_distance {
            best = Some((off, text_id, dist));
        }
    }

    let mut last: Option<(usize, u8)> = None;
    let mut i = script_start;
    while i < start {
        let op = buf[i];
        if op == 0x40 {
            let text_id = buf.get(i + 2).copied().unwrap_or(0);
            last = Some((i, text_id));
        }
        if op == 0x00 {
            break;
        }
        let sz = opcode_size_pc(buf, i, start);
        if sz == 0 {
            break;
        }
        i = i.saturating_add(sz);
    }

    if let Some((off, text_id)) = last {
        let dist = start.saturating_sub(off);
        if dist <= max_distance {
            match best {
                None => best = Some((off, text_id, dist)),
                Some((_, _, best_dist)) if dist < best_dist => {
                    best = Some((off, text_id, dist));
                }
                _ => {}
            }
        }
    }

    // Fallback: if the structured scan did not pick a suitable candidate,
    // perform a local byte-wise search around the opcode within
    // max_distance and choose the closest MESSAGE before or after.
    let low = script_start.saturating_add(
        start.saturating_sub(max_distance).saturating_sub(script_start),
    );
    let high = std::cmp::min(script_end, start.saturating_add(max_distance));

    let mut pos = start;
    while pos > low {
        pos -= 1;
        if buf[pos] == 0x40 {
            let sz = opcode_size_pc(buf, pos, script_end);
            if sz >= 3 {
                let text_id = buf.get(pos + 2).copied().unwrap_or(0);
                let dist = start.saturating_sub(pos);
                match best {
                    None => best = Some((pos, text_id, dist)),
                    Some((_, _, best_dist)) if dist < best_dist => {
                        best = Some((pos, text_id, dist));
                    }
                    _ => {}
                }
                break;
            }
        }
    }

    pos = start;
    while pos < high {
        if buf[pos] == 0x40 {
            let sz = opcode_size_pc(buf, pos, script_end);
            if sz >= 3 {
                let text_id = buf.get(pos + 2).copied().unwrap_or(0);
                let dist = pos.saturating_sub(start);
                match best {
                    None => best = Some((pos, text_id, dist)),
                    Some((_, _, best_dist)) if dist < best_dist => {
                        best = Some((pos, text_id, dist));
                    }
                    _ => {}
                }
                break;
            }
        }
        pos = pos.saturating_add(1);
    }

    best.map(|(off, text_id, _)| (off, text_id))
}

pub(crate) fn find_empty_text_slots(
    buf: &[u8],
    texts_base: usize,
    text_count: u16,
    positions: &[u16],
) -> Vec<u8> {
    let mut result = Vec::new();
    let total = text_count as usize;

    for id in 0..total {
        let start_rel = positions[id] as usize;
        let start = texts_base.saturating_add(start_rel);
        if start >= buf.len() {
            continue;
        }

        let next_start = if id + 1 < total {
            let rel = positions[id + 1] as usize;
            texts_base.saturating_add(rel.min(buf.len().saturating_sub(texts_base)))
        } else {
            buf.len()
        };

        if start >= next_start {
            continue;
        }

        let first = buf[start];
        if first == 0xFF {
            result.push(id as u8);
        }
    }

    result.sort();
    result
}

pub(crate) fn patch_pickup_text_in_place(
    buf: &mut [u8],
    texts_base: usize,
    text_count: u16,
    positions: &[u16],
    text_id: u8,
    qty: u8,
    item_id: u16,
    is_materia: bool,
) -> bool {
    let id = text_id as usize;
    if id >= text_count as usize {
        return false;
    }

    let start_rel = positions[id] as usize;
    let start = texts_base.saturating_add(start_rel);
    if start >= buf.len() {
        return false;
    }

    let next_start = if id + 1 < text_count as usize {
        let rel = positions[id + 1] as usize;
        texts_base.saturating_add(rel.min(buf.len().saturating_sub(texts_base)))
    } else {
        buf.len()
    };

    // Only treat bytes up to the first 0xFF as part of the text slot, as in
    // Makou's Section1File::open logic. This avoids overwriting hidden data or
    // adjacent structures within the text region.
    let mut end = start;
    while end < next_start {
        if buf[end] == 0xFF {
            end += 1;
            break;
        }
        end += 1;
    }
    if end <= start {
        return false;
    }

    let capacity = end - start;
    let msg_bytes = if let Some(b) = build_pickup_message_bytes(qty, item_id, is_materia, capacity)
    {
        b
    } else {
        return false;
    };

    let msg_len = msg_bytes.len();
    buf[start..start + msg_len].copy_from_slice(&msg_bytes);
    buf[start + msg_len] = 0xFF;
    for k in start + msg_len + 1..end {
        buf[k] = 0xFF;
    }
    true
}

fn build_key_message_bytes(key_name: &str, capacity: usize) -> Option<Vec<u8>> {
    let variants = [
        format!("Found \"{}\"!", key_name),
        format!("Found {}!", key_name),
        key_name.to_string(),
    ];

    for s in &variants {
        let bytes = encode_ff7_ascii(s);
        if bytes.len() + 1 <= capacity {
            return Some(bytes);
        }
    }

    None
}

pub(crate) fn patch_key_text_in_place(
    buf: &mut [u8],
    texts_base: usize,
    text_count: u16,
    positions: &[u16],
    text_id: u8,
    key_name: &str,
) -> bool {
    let id = text_id as usize;
    if id >= text_count as usize {
        return false;
    }

    let start_rel = positions[id] as usize;
    let start = texts_base.saturating_add(start_rel);
    if start >= buf.len() {
        return false;
    }

    let next_start = if id + 1 < text_count as usize {
        let rel = positions[id + 1] as usize;
        texts_base.saturating_add(rel.min(buf.len().saturating_sub(texts_base)))
    } else {
        buf.len()
    };

    let mut end = start;
    while end < next_start {
        if buf[end] == 0xFF {
            end += 1;
            break;
        }
        end += 1;
    }
    if end <= start {
        return false;
    }

    let capacity = end - start;
    let msg_bytes = if let Some(b) = build_key_message_bytes(key_name, capacity) {
        b
    } else {
        return false;
    };

    let msg_len = msg_bytes.len();
    buf[start..start + msg_len].copy_from_slice(&msg_bytes);
    buf[start + msg_len] = 0xFF;
    for k in start + msg_len + 1..end {
        buf[k] = 0xFF;
    }
    true
}

pub(crate) fn add_dialog_entry_for_pickup(
    buf: &mut Vec<u8>,
    qty: u8,
    item_id: u16,
    is_materia: bool,
) -> Option<u8> {
    if buf.len() < 6 + 9 * 4 {
        return None;
    }

    let section_count =
        u32::from_le_bytes([buf[2], buf[3], buf[4], buf[5]]) as usize;
    if section_count == 0 || section_count > 9 {
        return None;
    }

    let mut section_positions = vec![0u32; section_count];
    let mut o = 6usize;
    for i in 0..section_count {
        if o + 4 > buf.len() {
            return None;
        }
        section_positions[i] = u32::from_le_bytes([
            buf[o],
            buf[o + 1],
            buf[o + 2],
            buf[o + 3],
        ]);
        o += 4;
    }

    let s0 = section_positions[0] as usize;
    if s0 + 4 > buf.len() {
        return None;
    }
    let sec1_start = s0 + 4;
    let sec1_end = if section_count > 1 {
        section_positions[1] as usize
    } else {
        buf.len()
    };
    if sec1_start >= sec1_end || sec1_end > buf.len() {
        return None;
    }

    if sec1_start + 32 > buf.len() {
        return None;
    }
    let n_entities = buf[sec1_start + 2] as usize;
    let n_akao =
        u16::from_le_bytes([buf[sec1_start + 6], buf[sec1_start + 7]]) as usize;
    let pos_texts =
        u16::from_le_bytes([buf[sec1_start + 4], buf[sec1_start + 5]]) as usize;
    if pos_texts == 0 {
        return None;
    }
    let texts_base = sec1_start + pos_texts;
    if texts_base + 4 > buf.len() {
        return None;
    }

    let entities_off = sec1_start + 32;
    let akao_offsets_off = entities_off + n_entities.saturating_mul(8);
    if akao_offsets_off + n_akao.saturating_mul(4) > buf.len() {
        return None;
    }

    let (layout_texts_base, text_count, positions) =
        if let Some(v) = get_pc_field_text_layout(buf) {
            v
        } else {
            return None;
        };
    if layout_texts_base != texts_base {
        return None;
    }

    let total = text_count as usize;

    let mut text_region_end = sec1_end;
    if n_akao > 0 {
        let mut min_rel = usize::MAX;
        for j in 0..n_akao {
            let off = akao_offsets_off + j * 4;
            let rel = u32::from_le_bytes([
                buf[off],
                buf[off + 1],
                buf[off + 2],
                buf[off + 3],
            ]) as usize;
            if rel != 0 && rel < min_rel {
                min_rel = rel;
            }
        }
        if min_rel != usize::MAX {
            let cand = sec1_start + min_rel;
            if cand > texts_base && cand <= sec1_end {
                text_region_end = cand;
            }
        }
    }

    if text_region_end <= texts_base {
        return None;
    }

    let mut strings: Vec<Vec<u8>> = Vec::with_capacity(total + 1);
    for idx in 0..total {
        let rel = positions[idx] as usize;
        let start = texts_base.saturating_add(rel);
        if start >= text_region_end {
            return None;
        }
        let next_start = if idx + 1 < total {
            let r2 = positions[idx + 1] as usize;
            texts_base
                .saturating_add(r2.min(buf.len().saturating_sub(texts_base)))
        } else {
            text_region_end.min(buf.len())
        };
        if start >= next_start {
            strings.push(vec![0xFF]);
            continue;
        }
        let mut end = start;
        while end < next_start {
            let b = buf[end];
            end += 1;
            if b == 0xFF {
                break;
            }
        }
        if end <= start {
            strings.push(vec![0xFF]);
        } else {
            strings.push(buf[start..end].to_vec());
        }
    }

    let name = if is_materia {
        let mid = (item_id & 0xFF) as u8;
        lookup_materia_name(mid)
    } else {
        lookup_inventory_name(item_id)
    };
    let s = if qty > 1 {
        format!("Found x{} \"{}\"!", qty, name)
    } else {
        format!("Found \"{}\"!", name)
    };
    let mut msg_bytes = encode_ff7_ascii(&s);
    msg_bytes.push(0xFF);

    let new_id = strings.len() as u8;
    strings.push(msg_bytes);

    let new_count = strings.len() as u16;
    let mut new_block: Vec<u8> = Vec::new();
    new_block.extend_from_slice(&new_count.to_le_bytes());
    let mut cur_off = 2 + (new_count as usize) * 2;
    let mut ptrs: Vec<u16> = Vec::with_capacity(strings.len());
    for s_bytes in &strings {
        ptrs.push(cur_off as u16);
        cur_off = cur_off.saturating_add(s_bytes.len());
    }
    for p in &ptrs {
        new_block.extend_from_slice(&p.to_le_bytes());
    }
    for s_bytes in &strings {
        new_block.extend_from_slice(s_bytes);
    }

    let old_len = text_region_end.saturating_sub(texts_base);
    let new_len = new_block.len();
    let delta = new_len as isize - old_len as isize;
    let text_region_start = texts_base;
    let text_region_end_old = text_region_end;
    buf.splice(text_region_start..text_region_end_old, new_block.into_iter());

    if delta != 0 {
        let delta_i = delta;
        if n_akao > 0 {
            for j in 0..n_akao {
                let off = akao_offsets_off + j * 4;
                let rel_old =
                    u32::from_le_bytes([buf[off], buf[off + 1], buf[off + 2], buf[off + 3]])
                        as isize;
                let boundary_rel =
                    (texts_base - sec1_start) as isize + old_len as isize;
                if rel_old >= boundary_rel {
                    let rel_new = rel_old + delta_i;
                    buf[off..off + 4]
                        .copy_from_slice(&(rel_new as u32).to_le_bytes());
                }
            }
        }

        let size0_i =
            u32::from_le_bytes([buf[s0], buf[s0 + 1], buf[s0 + 2], buf[s0 + 3]])
                as isize
                + delta_i;
        buf[s0..s0 + 4].copy_from_slice(&(size0_i as u32).to_le_bytes());

        for idx in 1..section_count {
            let pos_off = 6 + idx * 4;
            let p_old =
                u32::from_le_bytes([
                    buf[pos_off],
                    buf[pos_off + 1],
                    buf[pos_off + 2],
                    buf[pos_off + 3],
                ]) as isize;
            let p_new = p_old + delta_i;
            buf[pos_off..pos_off + 4]
                .copy_from_slice(&(p_new as u32).to_le_bytes());
        }
    }

    Some(new_id)
}

pub(crate) fn rewrite_vanilla_keystone_source(buf: &mut [u8]) -> bool {
    // Locate the scripts/texts Section1, then scan the event scripts for a
    // BITON 1,0,69,2 opcode (Var[1][69] Keystone bit). When found, replace the
    // entire script body with a simple MESSAGE+STITM+RET sequence compiled
    // from the DSL, padded with NOPs to preserve the original script length so
    // that offsets and section layout remain unchanged.

    if buf.len() < 6 + 9 * 4 {
        return false;
    }

    let section_count = u32::from_le_bytes([buf[2], buf[3], buf[4], buf[5]]) as usize;
    if section_count == 0 || section_count > 9 {
        return false;
    }

    let mut section_positions = vec![0u32; section_count];
    let mut o = 6usize;
    for i in 0..section_count {
        if o + 4 > buf.len() {
            return false;
        }
        section_positions[i] = u32::from_le_bytes([
            buf[o],
            buf[o + 1],
            buf[o + 2],
            buf[o + 3],
        ]);
        o += 4;
    }

    let s0 = section_positions[0] as usize;
    if s0 + 4 > buf.len() {
        return false;
    }
    let sec1_start = s0 + 4;
    let sec1_end = if section_count > 1 {
        section_positions[1] as usize
    } else {
        buf.len()
    };
    if sec1_start + 32 > sec1_end || sec1_end > buf.len() {
        return false;
    }

    let n_entities = buf[sec1_start + 2] as usize;
    let n_akao = u16::from_le_bytes([
        buf[sec1_start + 6],
        buf[sec1_start + 7],
    ]) as usize;
    let pos_texts = u16::from_le_bytes([
        buf[sec1_start + 4],
        buf[sec1_start + 5],
    ]) as usize;
    if pos_texts == 0 {
        return false;
    }
    let texts_base = sec1_start + pos_texts;
    if texts_base > sec1_end || texts_base > buf.len() {
        return false;
    }

    let entities_off = sec1_start + 32;
    let akao_offsets_off = entities_off + n_entities.saturating_mul(8);
    if akao_offsets_off + n_akao.saturating_mul(4) > sec1_end {
        return false;
    }

    let script_tables_off = akao_offsets_off + n_akao.saturating_mul(4);
    let scripts_start = script_tables_off + n_entities.saturating_mul(64);
    let scripts_end = texts_base;
    if scripts_start > scripts_end || scripts_end > buf.len() {
        return false;
    }

    #[derive(Clone, Copy)]
    struct ScriptMeta {
        entity: u8,
        index: u8,
        rel: u16,
    }

    let mut scripts: Vec<ScriptMeta> = Vec::new();
    for ent in 0..n_entities {
        for idx in 0..32usize {
            let base = script_tables_off + (ent * 32 + idx) * 2;
            if base + 2 > scripts_start {
                // Pointer tables must end before scripts_start.
                return false;
            }
            let rel = u16::from_le_bytes([buf[base], buf[base + 1]]);
            if rel != 0 {
                scripts.push(ScriptMeta {
                    entity: ent as u8,
                    index: idx as u8,
                    rel,
                });
            }
        }
    }

    if scripts.is_empty() {
        return false;
    }

    scripts.sort_by_key(|s| s.rel);

    let mut target_script: Option<(usize, usize)> = None; // (start, end)

    for (si, meta) in scripts.iter().enumerate() {
        let start_rel = meta.rel as usize;
        let start = scripts_start.saturating_add(start_rel);
        if start >= scripts_end {
            continue;
        }
        let end = if let Some(next) = scripts.get(si + 1) {
            let next_rel = next.rel as usize;
            scripts_start.saturating_add(next_rel.min(scripts_end.saturating_sub(scripts_start)))
        } else {
            scripts_end
        };
        if end <= start || end > scripts_end {
            continue;
        }

        let mut pos = start;
        while pos + 4 <= end {
            let op = buf[pos];
            if op == 0x82 {
                let banks = buf[pos + 1];
                let bank1 = banks >> 4;
                let bank2 = banks & 0x0F;
                let var = buf[pos + 2];
                let bit = buf[pos + 3];
                if bank1 == 1 && bank2 == 0 && var == 69 && bit == 2 {
                    target_script = Some((start, end));
                    break;
                }
            }

            let sz = opcode_size_pc(buf, pos, end);
            if sz == 0 {
                break;
            }
            pos = pos.saturating_add(sz);
        }

        if target_script.is_some() {
            break;
        }
    }

    let (script_start, script_end) = match target_script {
        Some(v) => v,
        None => return false,
    };

    if script_end <= script_start || script_end > scripts_end {
        return false;
    }

    let old_len = script_end - script_start;
    if old_len == 0 {
        return false;
    }

    // Replacement script: simple MESSAGE + STITM + RET. The MESSAGE will be
    // repointed and its text regenerated by the randomiser when field pickups
    // are patched.
    let src = "MESSAGE 0 0\nSTITM 0 0x0000 1\nRET";
    let mut new_bytes = match compile_script_from_str(src) {
        Ok(b) => b,
        Err(_) => return false,
    };

    if new_bytes.len() > old_len {
        // Be conservative: do not expand the script region.
        return false;
    }

    // Pad with NOP (0x5F) to preserve original script length.
    let pad_len = old_len - new_bytes.len();
    new_bytes.extend(std::iter::repeat(0x5F).take(pad_len));

    buf[script_start..script_end].copy_from_slice(&new_bytes);

    true
}

pub(crate) fn lzs_decompress_raw(data: &[u8]) -> std::result::Result<Vec<u8>, String> {
    // Direct port of qt-lzs-1.3 LZS::decompressAll (headerless) used by
    // Makou Reactor and other FF7 tools. This expects only the compressed
    // payload, without any 4-byte uncompressed-size header.

    if data.is_empty() {
        return Err("LZS stream is empty".to_string());
    }

    let file_size = data.len();
    // Preallocate up to 5x compressed size, as in qt-lzs.
    let size_alloc = std::cmp::min(file_size.saturating_mul(5), i32::MAX as usize);
    let mut out: Vec<u8> = vec![0u8; size_alloc];
    let mut cur_result: usize = 0;

    // Ring buffer of size 4096, initialised to 0 for first 4078 bytes.
    let mut text_buf = [0u8; 4096];
    let mut cur_buff: usize = 4078;

    let mut flag_byte: u16 = 0;
    let mut pos: usize = 0;

    loop {
        // Load next flag byte when needed.
        if ((flag_byte >> 1) & 0x100) == 0 {
            if pos >= file_size {
                out.truncate(cur_result);
                return Ok(out);
            }
            flag_byte = (data[pos] as u16) | 0xFF00;
            pos += 1;
        } else {
            flag_byte >>= 1;
        }

        if pos >= file_size {
            out.truncate(cur_result);
            return Ok(out);
        }

        if (flag_byte & 1) != 0 {
            // Literal byte.
            let c = data[pos];
            pos += 1;

            if cur_result >= out.len() {
                out.push(c);
            } else {
                out[cur_result] = c;
            }

            text_buf[cur_buff] = c;
            cur_buff = (cur_buff + 1) & 0x0FFF;
            cur_result += 1;
        } else {
            // Back-reference.
            if pos + 1 >= file_size {
                out.truncate(cur_result);
                return Ok(out);
            }

            let mut offset = data[pos] as u16;
            let mut length = data[pos + 1] as u16;
            pos += 2;

            offset |= (length & 0xF0) << 4;
            let end_index = (length & 0x0F) + 2 + offset;
            let mut i = offset;

            while i <= end_index {
                let c = text_buf[(i & 0x0FFF) as usize];

                if cur_result >= out.len() {
                    out.push(c);
                } else {
                    out[cur_result] = c;
                }

                text_buf[cur_buff] = c;
                cur_buff = (cur_buff + 1) & 0x0FFF;
                cur_result += 1;
                i += 1;
            }
        }
    }
}

pub(crate) fn lzs_decompress(data: &[u8]) -> std::result::Result<Vec<u8>, String> {
    if data.is_empty() {
        return Err("LZS stream is empty".to_string());
    }

    // Many FF7 PC LZS streams (including field files) begin with a 4-byte
    // header that stores the *compressed* payload size. Interpret the input
    // as [compressed_size][payload] first; if that fails, fall back to
    // treating it as a raw headerless payload.

    if data.len() >= 4 {
        let compressed_size = u32::from_le_bytes([data[0], data[1], data[2], data[3]]) as usize;
        let available = data.len() - 4;
        let payload_len = std::cmp::min(compressed_size, available);

        if payload_len > 0 {
            let payload = &data[4..4 + payload_len];

            if let Ok(buf) = lzs_decompress_raw(payload) {
                if !buf.is_empty() {
                    return Ok(buf);
                }
            }
        }
    }

    // Fallback: treat the entire buffer as a headerless LZS payload.
    let buf = lzs_decompress_raw(data)?;
    if buf.is_empty() {
        Err("LZS decompression produced empty output".to_string())
    } else {
        Ok(buf)
    }
}

pub(crate) fn lzs_compress(input: &[u8]) -> std::result::Result<Vec<u8>, String> {
    // Port of myst6re's qt-lzs-1.3 LZS::compress (Okumura LZSS variant) used
    // by many FF7 tools. This aims to produce a bitstream compatible with the
    // game's expectations for LZS-compressed data.

    if input.is_empty() {
        return Ok(Vec::new());
    }

    const N: usize = 4096; // dictionary size
    const F: usize = 18; // lookahead buffer size
    const NIL: i32 = 4096; // special index meaning "not used"

    // Direct equivalents of qt-lzs's static arrays.
    let mut lson: [i32; N + 1] = [0; N + 1]; // 0..4096
    let mut rson: [i32; N + 257] = [0; N + 257]; // 0..4352
    let mut dad: [i32; N + 1] = [0; N + 1]; // 0..4096
    let mut text_buf: [u8; N + F - 1] = [0; N + F - 1]; // 0..4112

    let size_data = input.len();
    let mut result: Vec<u8> = Vec::with_capacity(size_data / 2);

    let mut match_length: i32 = 0;
    let mut match_position: i32 = 0;

    fn insert_node(
        r: i32,
        text_buf: &[u8; N + F - 1],
        lson: &mut [i32; N + 1],
        rson: &mut [i32; N + 257],
        dad: &mut [i32; N + 1],
        match_position: &mut i32,
        match_length: &mut i32,
    ) {
        let mut cmp = 1;
        let key = r as usize;
        let mut p: i32 = (N as i32 + 1) + text_buf[key] as i32; // 4097 + key[0]

        lson[r as usize] = NIL;
        rson[r as usize] = NIL;
        *match_length = 0;

        loop {
            let p_usize = p as usize;
            if cmp >= 0 {
                if rson[p_usize] != NIL {
                    p = rson[p_usize];
                } else {
                    rson[p_usize] = r;
                    dad[r as usize] = p;
                    return;
                }
            } else if lson[p_usize] != NIL {
                p = lson[p_usize];
            } else {
                lson[p_usize] = r;
                dad[r as usize] = p;
                return;
            }

            let mut i = 1usize;
            while i < F {
                let c = text_buf[key + i] as i32 - text_buf[p as usize + i] as i32;
                cmp = c;
                if cmp != 0 {
                    break;
                }
                i += 1;
            }

            if (i as i32) > *match_length {
                *match_position = p;
                *match_length = i as i32;
                if *match_length >= F as i32 {
                    break;
                }
            }
        }

        let r_usize = r as usize;
        let p_usize = p as usize;

        dad[r_usize] = dad[p_usize];
        lson[r_usize] = lson[p_usize];
        rson[r_usize] = rson[p_usize];

        let left = lson[p_usize];
        if left != NIL {
            dad[left as usize] = r;
        }
        let right = rson[p_usize];
        if right != NIL {
            dad[right as usize] = r;
        }

        let parent = dad[p_usize];
        if parent != NIL {
            let parent_usize = parent as usize;
            if rson[parent_usize] == p {
                rson[parent_usize] = r;
            } else {
                lson[parent_usize] = r;
            }
        }

        dad[p_usize] = NIL;
    }

    fn delete_node(
        p: i32,
        lson: &mut [i32; N + 1],
        rson: &mut [i32; N + 257],
        dad: &mut [i32; N + 1],
    ) {
        let p_usize = p as usize;
        if dad[p_usize] == NIL {
            return; // not in tree
        }

        let q: i32;
        if rson[p_usize] == NIL {
            q = lson[p_usize];
        } else if lson[p_usize] == NIL {
            q = rson[p_usize];
        } else {
            let mut q_tmp = lson[p_usize];
            if rson[q_tmp as usize] != NIL {
                loop {
                    q_tmp = rson[q_tmp as usize];
                    if rson[q_tmp as usize] == NIL {
                        break;
                    }
                }
                let e = dad[q_tmp as usize] as usize;
                rson[e] = lson[q_tmp as usize];
                let e2 = lson[q_tmp as usize];
                if e2 != NIL {
                    dad[e2 as usize] = dad[q_tmp as usize];
                }
                lson[q_tmp as usize] = lson[p_usize];
                let e3 = lson[p_usize];
                if e3 != NIL {
                    dad[e3 as usize] = q_tmp;
                }
            }
            rson[q_tmp as usize] = rson[p_usize];
            let e4 = rson[p_usize];
            if e4 != NIL {
                dad[e4 as usize] = q_tmp;
            }
            q = q_tmp;
        }

        let parent = dad[p_usize];
        let q_usize = q as usize;
        dad[q_usize] = parent;

        let parent_usize = parent as usize;
        if rson[parent_usize] == p {
            rson[parent_usize] = q;
        } else {
            lson[parent_usize] = q;
        }

        dad[p_usize] = NIL;
    }

    // Initialise trees: rson[4097..4352] roots, dad[0..4095] = NIL.
    for i in (N + 1)..=(N + 256) {
        rson[i] = NIL;
    }
    for i in 0..N {
        dad[i] = NIL;
    }

    let mut code_buf = [0u8; 17];
    let mut code_buf_ptr: usize = 1;
    let mut mask: u8 = 1;

    let mut s: i32 = 0;
    let mut r: i32 = (N - F) as i32; // 4078

    // Clear initial buffer region.
    for i in 0..r as usize {
        text_buf[i] = 0;
    }

    // Read up to F bytes into text_buf[r..r+F-1].
    let mut data_pos: usize = 0;
    let mut len: i32 = 0;
    while len < F as i32 && data_pos < size_data {
        text_buf[(r + len) as usize] = input[data_pos];
        data_pos += 1;
        len += 1;
    }

    if len == 0 {
        return Ok(Vec::new());
    }

    // Insert the 18 strings starting at r-1, r-2, ..., r-18.
    let mut i = 1;
    while i <= F as i32 {
        insert_node(r - i, &text_buf, &mut lson, &mut rson, &mut dad, &mut match_position, &mut match_length);
        i += 1;
    }

    // Insert the string starting at r.
    insert_node(r, &text_buf, &mut lson, &mut rson, &mut dad, &mut match_position, &mut match_length);

    loop {
        if match_length > len {
            match_length = len;
        }

        if match_length <= 2 {
            // Not long enough match: send one literal byte.
            match_length = 1;
            code_buf[0] |= mask; // 'literal' flag
            code_buf[code_buf_ptr] = text_buf[r as usize];
            code_buf_ptr += 1;
        } else {
            // Send position and length pair.
            code_buf[code_buf_ptr] = match_position as u8;
            code_buf_ptr += 1;
            code_buf[code_buf_ptr] = (((match_position >> 4) & 0xF0)
                | (match_length - (2 + 1))) as u8;
            code_buf_ptr += 1;
        }

        mask <<= 1;
        if mask == 0 {
            // Flush up to 8 units of code.
            result.extend_from_slice(&code_buf[..code_buf_ptr]);
            code_buf[0] = 0;
            code_buf_ptr = 1;
            mask = 1;
        }

        let last_match_length = match_length;
        let mut i_local = 0;

        // Read new bytes while we have input.
        while i_local < last_match_length && data_pos < size_data {
            let c = input[data_pos] as i32;
            data_pos += 1;

            delete_node(s, &mut lson, &mut rson, &mut dad);
            text_buf[s as usize] = c as u8;
            if s < 17 {
                text_buf[(s + 4096) as usize] = c as u8;
            }

            s = (s + 1) & 4095;
            r = (r + 1) & 4095;
            insert_node(r, &text_buf, &mut lson, &mut rson, &mut dad, &mut match_position, &mut match_length);

            i_local += 1;
        }

        // After end of input: slide window without reading.
        while i_local < last_match_length {
            delete_node(s, &mut lson, &mut rson, &mut dad);
            s = (s + 1) & 4095;
            r = (r + 1) & 4095;
            len -= 1;
            if len > 0 {
                insert_node(r, &text_buf, &mut lson, &mut rson, &mut dad, &mut match_position, &mut match_length);
            }
            i_local += 1;
        }

        if len <= 0 {
            break;
        }
    }

    if code_buf_ptr > 1 {
        result.extend_from_slice(&code_buf[..code_buf_ptr]);
    }

    Ok(result)
}

pub(crate) fn patch_md1stin_for_early_materia(buf: &mut [u8]) -> Option<usize> {
    // Force all SETWORD writes to Var[2][28] (16-bit) in the md1stin field
    // script to use the value 0xFEFF, which according to Makou Reactor enables
    // Materia in the menu. This operates on decompressed field data.

    const OPCODE_SETWORD: u8 = 0x81;
    const TARGET_BANK: u8 = 0x02; // destination bank 2
    const TARGET_ADDR: u8 = 0x1C; // Var[2][28]

    let (script_start, script_end) =
        get_pc_field_script_range(buf).unwrap_or((0, buf.len()));
    if script_start >= script_end || script_end > buf.len() {
        return None;
    }

    // Describe the desired opcode using the mini Makou DSL so that the
    // exact byte encoding is centralised in one place.
    let patch_bytes = match compile_script_from_str("SETWORD 2 0x1C 0xFEFF") {
        Ok(bytes) => bytes,
        Err(_) => {
            return None;
        }
    };
    if patch_bytes.len() != 5 || patch_bytes[0] != OPCODE_SETWORD {
        return None;
    }

    let mut first_offset: Option<usize> = None;

    let mut i = script_start;
    while i + 4 < script_end {
        let op = buf[i];
        if op == OPCODE_SETWORD {
            let ds = buf[i + 1];
            let d = ds >> 4; // high nibble = destination bank
            let a = buf[i + 2];

            if d == TARGET_BANK && a == TARGET_ADDR {
                let end = i + patch_bytes.len();
                if end > script_end {
                    break;
                }

                buf[i..end].copy_from_slice(&patch_bytes);

                if first_offset.is_none() {
                    first_offset = Some(i);
                }
            }
        }

        let size = opcode_size_pc(buf, i, script_end);
        if size == 0 {
            break;
        }
        i = i.saturating_add(size);
    }

    first_offset
}
