use flate2::{read::GzDecoder, write::GzEncoder, Compression};
use rand::{rngs::StdRng, Rng, SeedableRng};
use std::io::{Read, Write};

use crate::{RandomiserError, RandomiserSettings, Result};

pub(crate) struct SceneArchive {
    scenes: Vec<Vec<u8>>, // decompressed scene files
}

pub(crate) fn parse_scene_archive(raw: &[u8]) -> Result<SceneArchive> {
    const BLOCK_SIZE: usize = 0x2000;
    const POINTER_TABLE_SIZE: usize = 0x40; // 16 * 4 bytes

    let mut scenes = Vec::new();
    let mut offset = 0usize;

    while offset + BLOCK_SIZE <= raw.len() {
        let block = &raw[offset..offset + BLOCK_SIZE];

        if block.len() < POINTER_TABLE_SIZE {
            return Err(RandomiserError::Config(
                "scene.bin block too small to contain pointer table".to_string(),
            ));
        }

        // Read up to 16 little-endian u32 pointers.
        let mut pointers = Vec::new();
        for i in 0..16usize {
            let base = i * 4;
            let p = u32::from_le_bytes([
                block[base],
                block[base + 1],
                block[base + 2],
                block[base + 3],
            ]);
            if p == 0xFFFF_FFFF {
                break;
            }
            pointers.push((p as usize) << 2);
        }

        let num_scenes = pointers.len();
        if num_scenes > 0 {
            let mut offsets = pointers;
            offsets.push(BLOCK_SIZE); // sentinel for end of last scene in block

            for i in 0..num_scenes {
                let start = offsets[i];
                let end = offsets[i + 1];
                if end < start || end > BLOCK_SIZE {
                    return Err(RandomiserError::Config(
                        "scene.bin block has invalid scene offsets".to_string(),
                    ));
                }

                let mut slice = &block[start..end];
                // Strip trailing 0xFF padding.
                while !slice.is_empty() && *slice.last().unwrap() == 0xFF {
                    slice = &slice[..slice.len() - 1];
                }

                let mut decoder = GzDecoder::new(slice);
                let mut scene = Vec::new();
                decoder.read_to_end(&mut scene)?;

                // Accept both new (0x1E80) and old (0x1C50) scene formats.
                if scene.len() != 0x1E80 && scene.len() != 0x1C50 {
                    return Err(RandomiserError::Config(format!(
                        "scene.bin: scene has unexpected length {} bytes",
                        scene.len()
                    )));
                }

                scenes.push(scene);
            }
        }

        offset += BLOCK_SIZE;
    }

    Ok(SceneArchive { scenes })
}

pub(crate) fn summarize_scene_enemy_drops(archive: &SceneArchive) -> (usize, usize) {
    // Returns (enemies_with_any_drop, total_drop_slots).
    const NEW_SCENE_SIZE: usize = 0x1E80;
    const ENEMY_DATA_OFFSET: usize = 0x298;
    const ENEMY_DATA_SIZE: usize = 0xB8;
    const ENEMIES_PER_SCENE: usize = 3;

    let mut enemies_with_drop = 0usize;
    let mut total_drop_slots = 0usize;

    for scene in &archive.scenes {
        if scene.len() != NEW_SCENE_SIZE {
            continue; // ignore old-format scenes for now
        }

        for enemy_index in 0..ENEMIES_PER_SCENE {
            let base = ENEMY_DATA_OFFSET + enemy_index * ENEMY_DATA_SIZE;
            if base + 0x94 > scene.len() {
                continue;
            }

            let rates_off = base + 0x88;
            let items_off = base + 0x8C;
            if items_off + 8 > scene.len() {
                continue;
            }

            let rates = &scene[rates_off..rates_off + 4];
            let mut has_drop = false;

            for slot in 0..4 {
                let rate = rates[slot];
                // Only count slots that are actual drops (rate < 0x80) and
                // have a non-FFFF item ID.
                if rate < 0x80 {
                    let idx = items_off + slot * 2;
                    let item_id = u16::from_le_bytes([scene[idx], scene[idx + 1]]);
                    if item_id != 0xFFFF {
                        total_drop_slots += 1;
                        has_drop = true;
                    }
                }
            }

            if has_drop {
                enemies_with_drop += 1;
            }
        }
    }

    (enemies_with_drop, total_drop_slots)
}

pub(crate) fn randomize_enemy_drops_in_scene_archive(
    archive: &mut SceneArchive,
    settings: &RandomiserSettings,
) {
    // We currently shuffle drop items globally across all enemies, using only
    // items that already appear in drop slots. This guarantees we don't
    // introduce new key items or equipment drops beyond what vanilla already
    // has. In a later pass we can refine the pool using kernel item metadata.

    const NEW_SCENE_SIZE: usize = 0x1E80;
    const ENEMY_DATA_OFFSET: usize = 0x298;
    const ENEMY_DATA_SIZE: usize = 0xB8;
    const ENEMIES_PER_SCENE: usize = 3;

    let mut drop_pool: Vec<u16> = Vec::new();

    // First pass: collect all existing drop item IDs.
    for scene in &archive.scenes {
        if scene.len() != NEW_SCENE_SIZE {
            continue;
        }

        for enemy_index in 0..ENEMIES_PER_SCENE {
            let base = ENEMY_DATA_OFFSET + enemy_index * ENEMY_DATA_SIZE;
            if base + 0x94 > scene.len() {
                continue;
            }

            let rates_off = base + 0x88;
            let items_off = base + 0x8C;
            if items_off + 8 > scene.len() {
                continue;
            }

            let rates = &scene[rates_off..rates_off + 4];

            for slot in 0..4 {
                let rate = rates[slot];
                if rate < 0x80 {
                    let idx = items_off + slot * 2;
                    let item_id = u16::from_le_bytes([scene[idx], scene[idx + 1]]);
                    if item_id != 0xFFFF {
                        drop_pool.push(item_id);
                    }
                }
            }
        }
    }

    if drop_pool.is_empty() {
        return;
    }

    let mut rng = StdRng::seed_from_u64(settings.seed ^ 0xD0D0_D0D0_u64);

    // Second pass: shuffle drop items in-place.
    for scene in &mut archive.scenes {
        if scene.len() != NEW_SCENE_SIZE {
            continue;
        }

        for enemy_index in 0..ENEMIES_PER_SCENE {
            let base = ENEMY_DATA_OFFSET + enemy_index * ENEMY_DATA_SIZE;
            if base + 0x94 > scene.len() {
                continue;
            }

            let rates_off = base + 0x88;
            let items_off = base + 0x8C;
            if items_off + 8 > scene.len() {
                continue;
            }

            for slot in 0..4 {
                let rate = scene[rates_off + slot];
                if rate < 0x80 {
                    let idx = items_off + slot * 2;
                    let item_id = u16::from_le_bytes([scene[idx], scene[idx + 1]]);
                    if item_id != 0xFFFF {
                        let new_item = drop_pool[rng.gen_range(0..drop_pool.len())];
                        let bytes = new_item.to_le_bytes();
                        scene[idx] = bytes[0];
                        scene[idx + 1] = bytes[1];
                    }
                }
            }
        }
    }
}

fn decode_enemy_name_from_scene_block(scene: &[u8], base: usize) -> Option<String> {
    const ENEMY_NAME_OFFSET: usize = 0x00;
    const ENEMY_NAME_LEN: usize = 32;

    if base + ENEMY_NAME_OFFSET + ENEMY_NAME_LEN > scene.len() {
        return None;
    }

    let bytes = &scene[base + ENEMY_NAME_OFFSET..base + ENEMY_NAME_OFFSET + ENEMY_NAME_LEN];
    let mut out = String::new();

    for &code in bytes {
        if code == 0xFF {
            break;
        }
        let ch = code.wrapping_add(0x20);
        if (0x20..=0x7E).contains(&ch) {
            out.push(ch as char);
        }
    }

    if out.trim().is_empty() {
        None
    } else {
        Some(out)
    }
}

fn is_named_boss_enemy(name: &str) -> bool {
    let upper = name.to_ascii_uppercase();

    // Any Jenova or Sephiroth variant is always treated as a boss, regardless
    // of punctuation between the words.
    if upper.contains("JENOVA") || upper.contains("SEPHIROTH") {
        return true;
    }

    matches!(
        upper.as_str(),
        // Early / mid-game scripted bosses and special encounters.
        "GUARD SCORPION"
            | "MYSTERY NINJA"
            | "ULTIMATE WEAPON"
            | "AIR BUSTER"
            | "APS"
            | "TURKS:RENO"
            | "PYRAMID"
            | "SAMPLE:H0512"
            | "SAMPLE:H0512-OPT"
            | "RUFUS"
            | "DARK NATION"
            | "MIDGAR ZOLOM"
            | "BOTTOMSWELL"
            | "WATERPOLO"
            | "TURKS:RUDE"
            | "GI SPECTOR"
            | "SOUL FIRE"
            | "GI NATTAK"
            | "MATERIA KEEPER"
            | "LOST NUMBER"
            | "PALMER"
            | "RAPPS"
            | "GORKII"
            | "SHAKE"
            | "CHEKHOV"
            | "STANIV"
            | "GODO"
            | "DEMONS GATE"
            | "RED DRAGON"
            | "SNOW"
            | "MOTOR BALL"
            | "HELI GUNNER"
            | "HUNDRED GUNNER"
            | "SCHIZO(RIGHT)"
            | "SCHIZO(LEFT)"
            | "CARRY ARMOR"
            | "RIGHT ARM"
            | "LEFT ARM"
            | "TURKS:ELENA"
            | "PROUD CLOD"
            | "JAMAR ARMOR"
            | "HOJO"
            | "BAD RAP SAMPLE"
            | "POODLER SAMPLE"
            | "HELLETIC HOJO"
            | "LIFEFORM-HOJO N"
            | "DIAMOND WEAPON"
            | "RUBY WEAPON"
            | "RUBY'S TENTACLE"
            | "EMERALD WEAPON"
            | "LEG"
            | "EYE"
    )
}

pub(crate) fn randomize_enemy_formations_in_scene_archive(
    archive: &mut SceneArchive,
    settings: &RandomiserSettings,
) {
    const NEW_SCENE_SIZE: usize = 0x1E80;
    const ENEMY_DATA_OFFSET: usize = 0x298;
    const ENEMY_DATA_SIZE: usize = 0xB8;
    const ENEMIES_PER_SCENE: usize = 3;

    // Enemy data offsets within each ENEMY_DATA_SIZE block, from the
    // Battle_Scenes documentation (ff7-flat-wiki):
    const ENEMY_LEVEL_OFFSET: usize = 0x20; // 1 byte
    const ENEMY_STR_OFFSET: usize = 0x24; // 1 byte
    const ENEMY_DEF_OFFSET: usize = 0x25; // 1 byte
    const ENEMY_MAG_OFFSET: usize = 0x26; // 1 byte
    const ENEMY_MDEF_OFFSET: usize = 0x27; // 1 byte
    const ENEMY_HP_OFFSET: usize = 0xA4; // 4 bytes u32
    const ENEMY_EXP_OFFSET: usize = 0xA8; // 4 bytes u32
    const ENEMY_GIL_OFFSET: usize = 0xAC; // 4 bytes u32

    fn is_probable_boss(hp: u32, level: u8) -> bool {
        // Be conservative: treat anything reasonably bulky or high-level as a
        // probable boss so we avoid shuffling those scenes at all. This keeps
        // story / scripted battles much more stable while still letting us
        // randomise truly regular encounters.
        if hp == 0 {
            return false;
        }
        if hp >= 10_000 {
            return true;
        }
        if level >= 45 {
            return true;
        }
        false
    }

    fn scene_stat_scale_factor(scene_index: usize) -> f32 {
        // Make the early game noticeably easier, then ramp stats up slowly
        // through Midgar and more sharply into late game.
        const EASY_END_SCENE: usize = 64; // roughly covers early/Midgar content
        const MID_END_SCENE: usize = 160;
        const MAX_SCENE_INDEX: usize = 255;

        if scene_index <= EASY_END_SCENE {
            // Early scenes: scale from 0.3 (very weak) up to 1.0 (vanilla).
            // This ensures even late-game enemies are manageable when shuffled early.
            let t = (scene_index as f32 / EASY_END_SCENE as f32).clamp(0.0, 1.0);
            return 0.3 + t * 0.7;
        }

        if scene_index <= MID_END_SCENE {
            // Mid-game: ramp gently from 1.0 up to ~1.4.
            let span = (MID_END_SCENE - EASY_END_SCENE) as f32;
            let t = ((scene_index - EASY_END_SCENE) as f32 / span).clamp(0.0, 1.0);
            return 1.0 + t * 0.4;
        }

        // Late game: ramp from ~1.4 up to ~2.2.
        let span = (MAX_SCENE_INDEX - MID_END_SCENE) as f32;
        let t = (scene_index.saturating_sub(MID_END_SCENE) as f32 / span).clamp(0.0, 1.0);
        1.4 + t * 0.8
    }
    
    fn get_hp_cap_for_scene(scene_index: usize) -> u32 {
        // Hard caps on HP to prevent overpowered enemies in early areas.
        // Reference: Early enemies after Midgar have ~300-500 HP.
        const EARLY_END_SCENE: usize = 64;
        const MID_END_SCENE: usize = 160;
        
        if scene_index <= EARLY_END_SCENE {
            // Reactor 1 through Midgar: cap HP to keep it manageable.
            // Ramp from 200 HP (scene 8) to 800 HP (scene 64).
            let t = ((scene_index.saturating_sub(8)) as f32 / (EARLY_END_SCENE - 8) as f32).clamp(0.0, 1.0);
            let cap = 200.0 + t * 600.0;
            cap as u32
        } else if scene_index <= MID_END_SCENE {
            // Post-Midgar to mid-game: ramp from 800 to 5000 HP.
            let span = (MID_END_SCENE - EARLY_END_SCENE) as f32;
            let t = ((scene_index - EARLY_END_SCENE) as f32 / span).clamp(0.0, 1.0);
            let cap = 800.0 + t * 4200.0;
            cap as u32
        } else {
            // Late game: no cap, let scaling handle it.
            9_999_999
        }
    }

    // Phase 1a: Identify boss and non-boss scenes separately.
    // Bosses will be shuffled among themselves, non-bosses among themselves.
    // Keep a small number of the earliest scenes fixed so that the very first
    // encounters (tutorial fights) stay vanilla.

    const EARLY_SAFE_SCENES: usize = 8;

    let mut candidate_scenes: Vec<usize> = Vec::new();
    let mut boss_scenes: Vec<usize> = Vec::new();

    for (scene_index, scene) in archive.scenes.iter().enumerate() {
        // Do not randomise the earliest scenes so that the opening portion of
        // the game stays close to vanilla in both formations and stats.
        if scene_index < EARLY_SAFE_SCENES {
            continue;
        }

        if scene.len() != NEW_SCENE_SIZE {
            continue;
        }

        let mut has_boss = false;
        for enemy_index in 0..ENEMIES_PER_SCENE {
            let base = ENEMY_DATA_OFFSET + enemy_index * ENEMY_DATA_SIZE;
            if base + ENEMY_DATA_SIZE > scene.len() {
                continue;
            }

            if let Some(name) = decode_enemy_name_from_scene_block(scene, base) {
                if is_named_boss_enemy(&name) {
                    has_boss = true;
                    break;
                }
            }

            let level_off = base + ENEMY_LEVEL_OFFSET;
            if level_off >= scene.len() {
                continue;
            }
            let level = scene[level_off];

            let hp_off = base + ENEMY_HP_OFFSET;
            if hp_off + 4 > scene.len() {
                continue;
            }
            let hp = u32::from_le_bytes([
                scene[hp_off],
                scene[hp_off + 1],
                scene[hp_off + 2],
                scene[hp_off + 3],
            ]);
            if hp == 0 {
                continue;
            }

            if is_probable_boss(hp, level) {
                has_boss = true;
                break;
            }
        }

        if has_boss {
            boss_scenes.push(scene_index);
        } else {
            candidate_scenes.push(scene_index);
        }
    }

    if candidate_scenes.len() >= 2 {
        // Only clone the scenes we're actually going to shuffle, not the entire archive.
        let mut original_scenes: Vec<Vec<u8>> = Vec::with_capacity(candidate_scenes.len());
        for &idx in &candidate_scenes {
            original_scenes.push(archive.scenes[idx].clone());
        }
        let mut perm = candidate_scenes.clone();

        let mut rng = StdRng::seed_from_u64(settings.seed ^ 0xA1B2_C3D4_u64);
        let mut i = perm.len();
        while i > 1 {
            i -= 1;
            let j = rng.gen_range(0..=i);
            if i != j {
                perm.swap(i, j);
            }
        }

        for (i, &dst_scene_index) in candidate_scenes.iter().enumerate() {
            let src_scene_index = perm[i];
            if dst_scene_index == src_scene_index {
                continue;
            }

            let dst_scene = &mut archive.scenes[dst_scene_index];
            let src_scene = &original_scenes[i];

            if dst_scene.len() != NEW_SCENE_SIZE || src_scene.len() != NEW_SCENE_SIZE {
                continue;
            }

            // Swap the entire scene contents for candidate scenes rather than
            // only the enemy stat blocks. This keeps formations, camera data
            // and AI consistent with the enemies, avoiding mismatches that can
            // lead to battle softlocks.
            *dst_scene = src_scene.clone();
        }
    }

    // Phase 1b: Shuffle boss scenes among themselves.
    // This keeps bosses in boss slots but randomizes which boss you fight where.
    if settings.randomize_bosses && boss_scenes.len() >= 2 {
        let mut original_boss_scenes: Vec<Vec<u8>> = Vec::with_capacity(boss_scenes.len());
        for &idx in &boss_scenes {
            original_boss_scenes.push(archive.scenes[idx].clone());
        }
        let mut boss_perm = boss_scenes.clone();

        let mut rng = StdRng::seed_from_u64(settings.seed ^ 0xB055_FACE_u64);
        let mut i = boss_perm.len();
        while i > 1 {
            i -= 1;
            let j = rng.gen_range(0..=i);
            if i != j {
                boss_perm.swap(i, j);
            }
        }

        for (i, &dst_scene_index) in boss_scenes.iter().enumerate() {
            let src_scene_index = boss_perm[i];
            if dst_scene_index == src_scene_index {
                continue;
            }

            let dst_scene = &mut archive.scenes[dst_scene_index];
            let src_scene = &original_boss_scenes[i];

            if dst_scene.len() != NEW_SCENE_SIZE || src_scene.len() != NEW_SCENE_SIZE {
                continue;
            }

            // Swap entire boss scene to keep formations, AI, and camera consistent.
            *dst_scene = src_scene.clone();
        }
    }

    // Phase 2: apply scene-based stat scaling to non-boss enemies.
    // Skip early scenes that were excluded from shuffling to keep them truly vanilla.
    for (scene_index, scene) in archive.scenes.iter_mut().enumerate() {
        if scene_index < EARLY_SAFE_SCENES {
            continue;
        }

        if scene.len() != NEW_SCENE_SIZE {
            continue;
        }

        let scale = scene_stat_scale_factor(scene_index);
        if (scale - 1.0).abs() < f32::EPSILON {
            continue;
        }

        for enemy_index in 0..ENEMIES_PER_SCENE {
            let base = ENEMY_DATA_OFFSET + enemy_index * ENEMY_DATA_SIZE;
            if base + ENEMY_DATA_SIZE > scene.len() {
                continue;
            }

            let level_off = base + ENEMY_LEVEL_OFFSET;
            if level_off >= scene.len() {
                continue;
            }
            let level = scene[level_off];

            let hp_off = base + ENEMY_HP_OFFSET;
            let exp_off = base + ENEMY_EXP_OFFSET;
            let gil_off = base + ENEMY_GIL_OFFSET;
            if gil_off + 4 > scene.len() {
                continue;
            }

            let old_hp = u32::from_le_bytes([
                scene[hp_off],
                scene[hp_off + 1],
                scene[hp_off + 2],
                scene[hp_off + 3],
            ]);
            if old_hp == 0 {
                continue;
            }

            if let Some(name) = decode_enemy_name_from_scene_block(scene, base) {
                if is_named_boss_enemy(&name) {
                    continue;
                }
            }

            if is_probable_boss(old_hp, level) {
                continue;
            }

            let old_exp = u32::from_le_bytes([
                scene[exp_off],
                scene[exp_off + 1],
                scene[exp_off + 2],
                scene[exp_off + 3],
            ]);
            let old_gil = u32::from_le_bytes([
                scene[gil_off],
                scene[gil_off + 1],
                scene[gil_off + 2],
                scene[gil_off + 3],
            ]);

            // Scale HP with the scene difficulty factor, then apply a hard cap
            // to prevent late-game enemies from being too strong in early areas.
            let hp_cap = get_hp_cap_for_scene(scene_index);
            let new_hp_f = (old_hp as f32 * scale).round().clamp(1.0, hp_cap as f32);
            let new_hp = new_hp_f as u32;

            // Scale core stats (level, strength, defense, magic, magic defense)
            // with the same factor so enemies that appear early are also less
            // dangerous offensively/defensively.
            let stat_scale = scale;

            let new_level_f = (level as f32 * stat_scale).round().clamp(1.0, 99.0);
            let new_level = new_level_f as u8;

            let str_off = base + ENEMY_STR_OFFSET;
            let def_off = base + ENEMY_DEF_OFFSET;
            let mag_off = base + ENEMY_MAG_OFFSET;
            let mdef_off = base + ENEMY_MDEF_OFFSET;

            let old_str = scene[str_off];
            let old_def = scene[def_off];
            let old_mag = scene[mag_off];
            let old_mdef = scene[mdef_off];

            let new_str_f = (old_str as f32 * stat_scale).round().clamp(1.0, 255.0);
            let new_def_f = (old_def as f32 * stat_scale).round().clamp(1.0, 255.0);
            let new_mag_f = (old_mag as f32 * stat_scale).round().clamp(1.0, 255.0);
            let new_mdef_f = (old_mdef as f32 * stat_scale).round().clamp(1.0, 255.0);

            // Use a milder factor for rewards so EXP/Gil don't explode.
            let reward_scale = 0.5 * (1.0 + scale);
            let new_exp_f = (old_exp as f32 * reward_scale).round().clamp(0.0, 9_999_999.0);
            let new_gil_f = (old_gil as f32 * reward_scale).round().clamp(0.0, 9_999_999.0);
            let new_exp = new_exp_f as u32;
            let new_gil = new_gil_f as u32;

            scene[level_off] = new_level;
            scene[str_off] = new_str_f as u8;
            scene[def_off] = new_def_f as u8;
            scene[mag_off] = new_mag_f as u8;
            scene[mdef_off] = new_mdef_f as u8;

            scene[hp_off..hp_off + 4].copy_from_slice(&new_hp.to_le_bytes());
            scene[exp_off..exp_off + 4].copy_from_slice(&new_exp.to_le_bytes());
            scene[gil_off..gil_off + 4].copy_from_slice(&new_gil.to_le_bytes());
        }
    }
}

pub(crate) fn build_scene_archive(archive: &SceneArchive) -> Result<Vec<u8>> {
    const BLOCK_SIZE: usize = 0x2000;
    const POINTER_TABLE_SIZE: usize = 0x40;

    let mut out = Vec::new();

    let mut block: Vec<u8> = Vec::new();
    let mut pointers: Vec<usize> = Vec::new();

    let mut scene_index = 0usize;
    let num_scenes = archive.scenes.len();

    loop {
        let mut write_block = false;
        let mut cmp_data: Option<Vec<u8>> = None;

        if scene_index >= num_scenes {
            write_block = true;
        } else {
            // Compress next scene.
            let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
            encoder.write_all(&archive.scenes[scene_index])?;
            let mut data = encoder.finish()?;

            if data.len() % 4 != 0 {
                let pad = 4 - (data.len() % 4);
                data.extend(std::iter::repeat(0xFFu8).take(pad));
            }

            if POINTER_TABLE_SIZE + block.len() + data.len() > BLOCK_SIZE {
                // Doesn't fit; flush current block first.
                write_block = true;
                cmp_data = Some(data);
            } else {
                cmp_data = Some(data);
            }
        }

        if write_block {
            // Write current block (even if empty, at the very end).
            // Pointer table: 16 little-endian u32 values (offset >> 2) or 0xFFFFFFFF.
            for p in &pointers {
                let val = (*p as u32) >> 2;
                out.extend_from_slice(&val.to_le_bytes());
            }
            for _ in pointers.len()..16 {
                out.extend_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
            }

            if block.len() < BLOCK_SIZE - POINTER_TABLE_SIZE {
                let pad = BLOCK_SIZE - POINTER_TABLE_SIZE - block.len();
                block.extend(std::iter::repeat(0xFFu8).take(pad));
            }
            out.extend_from_slice(&block);

            block.clear();
            pointers.clear();

            if scene_index >= num_scenes {
                break;
            }
        }

        if let Some(data) = cmp_data {
            if POINTER_TABLE_SIZE + block.len() + data.len() > BLOCK_SIZE {
                // Extremely unlikely (scene too big to fit in an empty block),
                // but avoid infinite loop.
                return Err(RandomiserError::Config(
                    "scene.bin: compressed scene does not fit into a block".to_string(),
                ));
            }

            let ptr = POINTER_TABLE_SIZE + block.len();
            pointers.push(ptr);
            block.extend_from_slice(&data);
            scene_index += 1;
        }
    }

    Ok(out)
}

pub(crate) fn randomize_scene_bin(
    raw_scene: &[u8],
    settings: &RandomiserSettings,
) -> Result<(Vec<u8>, Option<(usize, usize)>)> {
    if !settings.randomize_enemy_drops && !settings.randomize_enemies {
        // No enemy-related randomisation enabled; return the original
        // scene.bin bytes untouched and without a drop summary.
        return Ok((raw_scene.to_vec(), None));
    }

    let mut archive = parse_scene_archive(raw_scene)?;

    let mut summary: Option<(usize, usize)> = None;

    if settings.randomize_enemy_drops {
        randomize_enemy_drops_in_scene_archive(&mut archive, settings);
        summary = Some(summarize_scene_enemy_drops(&archive));
    }

    if settings.randomize_enemies {
        randomize_enemy_formations_in_scene_archive(&mut archive, settings);
    }

    if settings.randomize_enemy_drops || settings.randomize_enemies {
        let new_scene_bytes = build_scene_archive(&archive)?;
        Ok((new_scene_bytes, summary))
    } else {
        Ok((raw_scene.to_vec(), None))
    }
}
