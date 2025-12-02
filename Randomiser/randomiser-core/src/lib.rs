use flate2::{read::GzDecoder, write::GzEncoder, Compression};
use rand::{rngs::StdRng, Rng, SeedableRng};
use rand::seq::SliceRandom;
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use thiserror::Error;

mod items;
mod shops;
mod scene;
pub mod field;
mod field_compiler;

use shops::build_shops_hext;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RandomiserSettings {
    pub seed: u64,
    pub randomize_enemy_drops: bool,
    pub randomize_enemies: bool,
    pub randomize_shops: bool,
    pub randomize_equipment: bool,
    pub randomize_starting_materia: bool,
    pub starting_materia_all_types: bool,
    pub randomize_starting_weapons: bool,
    pub randomize_starting_armor: bool,
    pub randomize_starting_accessories: bool,
    pub randomize_weapon_stats: bool,
    pub randomize_weapon_slots: bool,
    pub randomize_weapon_growth: bool,
    pub keep_weapon_appearance: bool,
    pub randomize_field_pickups: bool,
    pub debug: bool,
    pub input_path: PathBuf,
    pub output_path: PathBuf,
}

use items::{
    build_field_item_pool,
    lookup_inventory_name,
    make_huge_materia_perm,
    GUARANTEED_FIELD_ITEMS,
    HUGE_MATERIA_BITS,
};

use scene::{
    randomize_scene_bin,
};

#[derive(Debug, Error)]
pub enum RandomiserError {
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    #[error("configuration error: {0}")]
    Config(String),
}

pub type Result<T> = std::result::Result<T, RandomiserError>;

fn join_candidate(base: &Path, candidate: &str) -> PathBuf {
    let mut path = base.to_path_buf();
    for part in candidate.split(['/', '\\']) {
        if !part.is_empty() {
            path.push(part);
        }
    }
    path
}

fn find_first_existing(base: &Path, candidates: &[&str]) -> Option<PathBuf> {
    for candidate in candidates {
        let path = join_candidate(base, candidate);
        if path.exists() {
            return Some(path);
        }
    }
    None
}

struct KernelFile {
    dir_id: u16,
    index: u16,
    raw_size: u16,
    cmp_data: Vec<u8>,
}

struct KernelArchive {
    files: Vec<KernelFile>,
    trailer: Vec<u8>,
}

struct LgpEntry {
    name: String,
    offset: u32,
}

struct LgpArchive {
    creator: [u8; 12],
    entries: Vec<LgpEntry>,
}

#[derive(Clone)]
struct PickupSlot {
    entry_index: usize,
    field_name: String,
    opcode_off: usize,
    qty: u8,
    item_id: u16,
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
enum FieldZone {
    Midgar,
    MidgarRaid,
    ShinraBuilding,
    TempleAndAncients,
    Glacier,
    LateGame,
    WallMarket,
    ClimbToShinraBuilding,
    Nibelheim,
    Kalm,
    FarmAndMythrilMines,
    FortCondor,
    JunonUpperAndLower,
    Other,
}

fn classify_field_zone(name: &str, _entry_index: usize) -> FieldZone {
    let n = name.to_ascii_lowercase();

    if n.starts_with("blin") {
        return FieldZone::ShinraBuilding;
    }

    if n.starts_with("jtmp") || n.starts_with("loslake") || n.starts_with("ancnt") {
        return FieldZone::TempleAndAncients;
    }

    if n.starts_with("slfrst")
        || n.starts_with("snow")
        || n.starts_with("ice")
        || n.starts_with("icicle")
        || n.starts_with("gaia")
    {
        return FieldZone::Glacier;
    }

    if n.starts_with("trnad") {
        return FieldZone::LateGame;
    }

    if n.starts_with("mkt")
        || n.starts_with("onna")
        || n.starts_with("mrkt")
        || n.starts_with("colne")
    {
        return FieldZone::WallMarket;
    }

    if n.starts_with("wcrimb") {
        return FieldZone::ClimbToShinraBuilding;
    }

    // Late-game Midgar raid maps.
    if n == "md8_5"
        || n == "md8_6"
        || n == "md8_b1"
        || n == "md8_b2"
        || n == "sbwy4_22"
        || n == "tunnel_4"
        || n == "tunnel_5"
        || n == "md8brdg2"
        || n == "md8_32"
        || n == "canon_1"
        || n == "canon_2"
    {
        return FieldZone::MidgarRaid;
    }

    if n.starts_with("md")
        || n.starts_with("nmkin")
        || n.starts_with("nnmid")
        || n.starts_with("elmin")
        || n.starts_with("mrkt")
        || n.starts_with("smkin")
        || n.starts_with("sbwy")
        || n.starts_with("eal")
    {
        return FieldZone::Midgar;
    }

    if n.starts_with("niv")
        || n.starts_with("nv")
        || n.starts_with("sinin")
        || n.starts_with("mtnvl")
    {
        return FieldZone::Nibelheim;
    }

    if n.starts_with("elm") {
        return FieldZone::Kalm;
    }

    if n.starts_with("farm")
        || n.starts_with("psd")
        || n.starts_with("frmin")
        || n.starts_with("frcyo")
    {
        return FieldZone::FarmAndMythrilMines;
    }

    if n.starts_with("con") {
        return FieldZone::FortCondor;
    }

    if n.starts_with("jun")
        || n.starts_with("uju")
        || n.starts_with("pris")
    {
        return FieldZone::JunonUpperAndLower;
    }

    FieldZone::Other
}

fn key_can_appear_in_slot(
    flag: &items::KeyItemFlag,
    slot: &PickupSlot,
    field_order: &HashMap<String, usize>,
) -> bool {
    let name = flag.name;
    let field = slot.field_name.as_str();

    let entry_index = slot.entry_index;

    let zone = classify_field_zone(field, entry_index);

    let before = |limit_name: &str| -> bool {
        if let Some(&limit_idx) = field_order.get(limit_name) {
            entry_index <= limit_idx
        } else {
            true
        }
    };

    if name.starts_with("Keycard ") {
        if !(zone == FieldZone::ShinraBuilding
            || zone == FieldZone::Midgar
            || zone == FieldZone::WallMarket)
        {
            return false;
        }

        if name == "Keycard 60" && !before("blin60_1") {
            return false;
        }
        if name == "Keycard 62" && !before("blin62_1") {
            return false;
        }
        if name == "Keycard 65" && !before("blin63_1") {
            return false;
        }
        if name == "Keycard 66" && !before("blin66_1") {
            return false;
        }
        if name == "Keycard 68" && !before("blin69_1") {
            return false;
        }
    }

    if name.starts_with("Midgar Part #") {
        if !(zone == FieldZone::ShinraBuilding
            || zone == FieldZone::Midgar
            || zone == FieldZone::WallMarket)
        {
            return false;
        }

        if !before("blin65_1") {
            return false;
        }
    }

    if name == "Keystone" {
        if !before("jtempl") {
            return false;
        }
    }

    if name == "Key to Ancients" {
        if !before("snw_w") {
            return false;
        }
    }

    if name == "Lunar Harp" {
        if !before("slfrst_1") {
            return false;
        }
    }

    if name == "Glacier Map" || name == "Snowboard" {
        if !before("hyou1") {
            return false;
        }
    }

    if name == "Black Materia" {
        if !before("trnad_1") {
            return false;
        }
    }

    let _ = flag;
    true
}

fn build_key_item_placements(
    slots: &[PickupSlot],
    seed: u64,
    field_order: &HashMap<String, usize>,
) -> HashMap<(usize, usize), &'static items::KeyItemFlag> {
    let mut placements: HashMap<(usize, usize), &'static items::KeyItemFlag> =
        HashMap::new();

    if slots.is_empty() {
        return placements;
    }

    let mut flags: Vec<&'static items::KeyItemFlag> =
        items::key_item_flags_with_role(items::ItemRole::KeyProgression).collect();
    if flags.is_empty() {
        return placements;
    }

    let mut rng = StdRng::seed_from_u64(seed ^ 0x4B1D_0EAD_u64);
    flags.shuffle(&mut rng);

    let mut remaining_slots: Vec<PickupSlot> = slots.to_vec();

    for flag in flags {
        let mut chosen_index: Option<usize> = None;

        for (idx, slot) in remaining_slots.iter().enumerate() {
            if key_can_appear_in_slot(flag, slot, field_order) {
                chosen_index = Some(idx);
                break;
            }
        }

        if let Some(idx) = chosen_index {
            let slot = remaining_slots.swap_remove(idx);
            placements.insert((slot.entry_index, slot.opcode_off), flag);
        }
    }

    placements
}

fn randomize_field_pickups_in_flevel(
    flevel_bytes: &[u8],
    flevel_archive: &LgpArchive,
    settings: &RandomiserSettings,
) -> Result<(
    HashMap<String, Vec<u8>>,
    Option<usize>,
    Option<usize>,
    String,
    String,
)> {
    let field_count = flevel_archive.entries.len();

    let mut md1stin_setword_offset: Option<usize> = None;
    let mut md1stin_decompressed_len: Option<usize> = None;
    let mut field_replacements: HashMap<String, Vec<u8>> = HashMap::new();

    let mut field_order: HashMap<String, usize> = HashMap::new();
    for (idx, entry) in flevel_archive.entries.iter().enumerate() {
        let name = entry.name.to_ascii_lowercase();
        field_order.entry(name).or_insert(idx);
    }

    let mut key_pickup_slots: Vec<PickupSlot> = Vec::new();

    for (entry_index, entry) in flevel_archive.entries.iter().enumerate() {
        let off_usize = entry.offset as usize;
        const INNER_HEADER_SIZE: usize = 24;

        if off_usize + INNER_HEADER_SIZE > flevel_bytes.len() {
            continue;
        }

        let header_name_bytes = &flevel_bytes[off_usize..off_usize + 20];
        let nul_pos = header_name_bytes
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(header_name_bytes.len());
        let trimmed = &header_name_bytes[..nul_pos];
        let field_name = String::from_utf8_lossy(trimmed).trim_end().to_string();
        if field_name.is_empty() {
            continue;
        }

        // Skip known debug-only maps.
        if field_name.eq_ignore_ascii_case("blackbg1")
            || field_name.eq_ignore_ascii_case("blackbg2")
            || field_name.eq_ignore_ascii_case("blackbg3")
            || field_name.eq_ignore_ascii_case("blackbg4")
            || field_name.eq_ignore_ascii_case("blackbg5")
            || field_name.eq_ignore_ascii_case("blackbg6")
            || field_name.eq_ignore_ascii_case("tin_1")
        {
            continue;
        }

        let size_bytes = &flevel_bytes[off_usize + 20..off_usize + 24];
        let declared_len = u32::from_le_bytes([
            size_bytes[0],
            size_bytes[1],
            size_bytes[2],
            size_bytes[3],
        ]) as usize;

        let comp_start = off_usize + INNER_HEADER_SIZE;
        let file_end = flevel_archive
            .entries
            .iter()
            .filter_map(|e| {
                if e.offset > entry.offset {
                    Some(e.offset as usize)
                } else {
                    None
                }
            })
            .min()
            .unwrap_or(flevel_bytes.len());

        if comp_start >= file_end || file_end > flevel_bytes.len() {
            continue;
        }

        let cmp_bytes = flevel_bytes[comp_start..file_end].to_vec();

        if let Ok(buf) = field::lzs_decompress(&cmp_bytes) {
            let (scan_start, scan_end) =
                field::get_pc_field_script_range(&buf).unwrap_or((0, buf.len()));

            let mut i = scan_start;
            while i < scan_end {
                let opcode = buf[i];
                if opcode == 0x58 {
                    let off = i;
                    let banks = buf.get(i + 1).copied().unwrap_or(0);
                    let item_lo = buf.get(i + 2).copied().unwrap_or(0);
                    let item_hi = buf.get(i + 3).copied().unwrap_or(0);
                    let qty = buf.get(i + 4).copied().unwrap_or(0);
                    let item_id = u16::from_le_bytes([item_lo, item_hi]);

                    if banks == 0 && qty >= 1 && qty <= 99 {
                        key_pickup_slots.push(PickupSlot {
                            entry_index,
                            field_name: field_name.to_ascii_lowercase(),
                            opcode_off: off,
                            qty,
                            item_id,
                        });
                    }
                }

                let size = field::opcode_size_pc(&buf, i, scan_end);
                if size == 0 {
                    break;
                }
                i += size;
            }
        }
    }

    let key_item_placements =
        build_key_item_placements(&key_pickup_slots, settings.seed, &field_order);

    let mut field_index_log = String::new();
    let mut field_pickups_rand_log = String::new();
    let mut smtra_materia_pool: Vec<u8> = Vec::new();
    let huge_perm = make_huge_materia_perm(settings.seed);
    let mut full_item_pool = build_field_item_pool(&settings);
    let mut field_item_pool = full_item_pool.clone();
    let mut guaranteed_remaining: Vec<u16> = GUARANTEED_FIELD_ITEMS
        .iter()
        .copied()
        .filter(|id| full_item_pool.contains(id))
        .collect();

    let key_item_flags = items::all_key_item_flags();

    if !guaranteed_remaining.is_empty() {
        field_item_pool.retain(|id| !GUARANTEED_FIELD_ITEMS.contains(id));
    }

    // Guarantee up to three Battery (0x0055) pickups before wcrimb_1 by
    // forcing eligible early STITM slots to that item ID.
    let battery_item_id: u16 = 0x0055;
    let battery_limit_index = field_order
        .get("wcrimb_1")
        .copied()
        .unwrap_or(field_count.saturating_sub(1));
    let mut guaranteed_batteries_remaining: u8 = 3;

    for (entry_index, entry) in flevel_archive.entries.iter().enumerate() {
        let off_usize = entry.offset as usize;
        const INNER_HEADER_SIZE: usize = 24;

        if off_usize + INNER_HEADER_SIZE > flevel_bytes.len() {
            continue;
        }

        let header_name_bytes = &flevel_bytes[off_usize..off_usize + 20];
        let nul_pos = header_name_bytes
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(header_name_bytes.len());
        let trimmed = &header_name_bytes[..nul_pos];
        let field_name = String::from_utf8_lossy(trimmed).trim_end().to_string();
        if field_name.is_empty() {
            continue;
        }

        if field_name.eq_ignore_ascii_case("blackbg1")
            || field_name.eq_ignore_ascii_case("blackbg2")
            || field_name.eq_ignore_ascii_case("blackbg3")
            || field_name.eq_ignore_ascii_case("blackbg4")
            || field_name.eq_ignore_ascii_case("blackbg5")
            || field_name.eq_ignore_ascii_case("blackbg6")
            || field_name.eq_ignore_ascii_case("tin_1")
        {
            continue;
        }

        let size_bytes = &flevel_bytes[off_usize + 20..off_usize + 24];
        let declared_len = u32::from_le_bytes([
            size_bytes[0],
            size_bytes[1],
            size_bytes[2],
            size_bytes[3],
        ]) as usize;

        let comp_start = off_usize + INNER_HEADER_SIZE;
        let file_end = flevel_archive
            .entries
            .iter()
            .filter_map(|e| {
                if e.offset > entry.offset {
                    Some(e.offset as usize)
                } else {
                    None
                }
            })
            .min()
            .unwrap_or(flevel_bytes.len());

        if comp_start >= file_end || file_end > flevel_bytes.len() {
            continue;
        }

        let cmp_bytes = flevel_bytes[comp_start..file_end].to_vec();

        if let Ok(mut buf) = field::lzs_decompress(&cmp_bytes) {
            // Track whether we changed the field *before* running the
            // normal pickup randomisation, so that pure script
            // rewrites (like converting the original Keystone BITON
            // source into a STITM chest) are still written back.
            let mut changed = false;

            // Before doing any pickup randomisation, give the field
            // module a chance to apply a few targeted script
            // rewrites that convert specific key-item BITON sources
            // into simple chest-style scripts.
            if field_name.eq_ignore_ascii_case("clsin2_2") {
                // Emit a small debug scan of all BITON Var[1][69]
                // usages so we can cross-check against Makou.
                let debug = field::debug_scan_sc_keyitems(&buf);
                field_pickups_rand_log.push_str(&debug);

                // Then neutralise Dio's Keystone grant by
                // redirecting BITON Var[1][69] to a harmless local
                // var and blanking its nearby MESSAGE text. This
                // keeps the scene and other rewards intact while
                // ensuring only md1stin sets the Keystone bit.
                if field::neutralise_clsin2_2_keystone(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "neutralise_clsin2_2_keystone field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("blin65_1")
                || field_name.eq_ignore_ascii_case("blin_65_1")
            {
                // Install Midgar Part #1/#2 helper scripts as
                // Entity 15/16 Script 30, and rewire their
                // original BITONs into REQ calls so the
                // randomiser can treat them as normal pickups.
                if field::install_blin65_midgar_part1_req_script(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_blin65_midgar_part1_req_script field={} applied\n",
                        field_name,
                    ));
                }
                if field::install_blin65_midgar_part2_req_script(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_blin65_midgar_part2_req_script field={} applied\n",
                        field_name,
                    ));
                }
                if field::install_blin65_extra_midgar_helpers(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_blin65_extra_midgar_helpers field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("subin_1b") {
                if field::install_subin1b_key_to_ancients_req_script(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_subin1b_key_to_ancients_req_script field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("mkt_s1") {
                if field::install_mkt_s1_dress_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_mkt_s1_dress_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("mkt_mens") {
                if field::install_mkt_mens_wig_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_mkt_mens_wig_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("mkt_m") {
                if field::install_mkt_m_tiara_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_mkt_m_tiara_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("mktpb") {
                if field::install_mktpb_cologne_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_mktpb_cologne_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("onna_52") {
                if field::install_onna_52_underwear_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_onna_52_underwear_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            } else if field_name.eq_ignore_ascii_case("mkt_s3") {
                if field::install_mkt_s3_pharmacy_group_helper(&mut buf) {
                    changed = true;
                    field_pickups_rand_log.push_str(&format!(
                        "install_mkt_s3_pharmacy_group_helper field={} applied\n",
                        field_name,
                    ));
                }
            }

            let (scan_start, scan_end) =
                field::get_pc_field_script_range(&buf).unwrap_or((0, buf.len()));
            let mut text_layout = field::get_pc_field_text_layout(&buf);
            let mut empty_text_slots: Option<Vec<u8>> = text_layout.as_ref().map(
                |(texts_base, text_count, positions)| {
                    field::find_empty_text_slots(
                        &buf,
                        *texts_base,
                        *text_count,
                        positions,
                    )
                },
            );

            let mut total_stitm = 0usize;
            let mut const_stitm = 0usize;
            let mut const_itemish = 0usize;

            if field_name.eq_ignore_ascii_case("md1stin") {
                md1stin_decompressed_len = Some(buf.len());
                if let Some(off) = field::patch_md1stin_for_early_materia(&mut buf) {
                    md1stin_setword_offset = Some(off);
                    changed = true;
                }
            }

            let mut rng: Option<StdRng> = {
                let mut h: u64 = 0xcbf29ce484222325u64;
                for b in field_name.as_bytes() {
                    h ^= *b as u64;
                    h = h.wrapping_mul(0x100000001B3_u64);
                }
                let seed = settings.seed ^ h;
                Some(StdRng::seed_from_u64(seed))
            };

            let mut i = scan_start;
            while i < scan_end {
                let opcode = buf[i];
                if opcode == 0x58 {
                    total_stitm += 1;

                    let off = i;
                    let banks = buf.get(i + 1).copied().unwrap_or(0);
                    let item_lo = buf.get(i + 2).copied().unwrap_or(0);
                    let item_hi = buf.get(i + 3).copied().unwrap_or(0);
                    let qty = buf.get(i + 4).copied().unwrap_or(0);
                    let item_id = u16::from_le_bytes([item_lo, item_hi]);
                    if let Some(flag) = key_item_placements.get(&(entry_index, off)) {
                        let bit_mask = flag.bit;
                        if bit_mask.count_ones() == 1 && off + 4 < buf.len() {
                            let bit_index = bit_mask.trailing_zeros() as u8;
                            let banks_byte = (flag.bank << 4) | 0;

                            buf[off] = 0x82;
                            buf[off + 1] = banks_byte;
                            buf[off + 2] = flag.addr;
                            buf[off + 3] = bit_index;
                            buf[off + 4] = 0x5F;
                            changed = true;

                            let mut key_text_id: i32 = -1;
                            let mut key_text_patched = false;

                            if let Some((texts_base, text_count, positions)) =
                                text_layout.as_mut()
                            {
                                if let Some((_, text_id)) =
                                    field::find_nearby_message(
                                        &buf,
                                        scan_start,
                                        scan_end,
                                        i,
                                        0xC0,
                                    )
                                {
                                    key_text_id = text_id as i32;
                                    key_text_patched =
                                        field::patch_key_text_in_place(
                                            &mut buf,
                                            *texts_base,
                                            *text_count,
                                            positions,
                                            text_id,
                                            flag.name,
                                        );
                                }
                            }

                            field_pickups_rand_log.push_str(&format!(
                                "key_field={} off=0x{:06X} key_name={} bank={} addr={} bit=0x{:02X} text_id={} text_patched={}\n",
                                field_name,
                                off,
                                flag.name,
                                flag.bank,
                                flag.addr,
                                flag.bit,
                                key_text_id,
                                key_text_patched,
                            ));
                        }

                        let size = field::opcode_size_pc(&buf, i, scan_end);
                        if size == 0 {
                            break;
                        }
                        i += size;
                        continue;
                    }

                    if banks == 0 {
                        const_stitm += 1;

                        if qty >= 1 && qty <= 99 && full_item_pool.contains(&item_id) {
                            const_itemish += 1;
                            if let Some(r) = rng.as_mut() {
                                let mut new_item_id: u16;

                                // First, guarantee up to three Batteries before wcrimb_1.
                                if guaranteed_batteries_remaining > 0
                                    && entry_index <= battery_limit_index
                                {
                                    new_item_id = battery_item_id;
                                    guaranteed_batteries_remaining -= 1;
                                } else if let Some(id) = guaranteed_remaining.pop() {
                                    new_item_id = id;
                                } else {
                                    if field_item_pool.is_empty() {
                                        continue;
                                    }
                                    let new_item_idx = r.gen_range(0..field_item_pool.len());
                                    new_item_id = field_item_pool[new_item_idx];
                                }

                                let new_bytes = new_item_id.to_le_bytes();
                                if i + 4 < buf.len() {
                                    buf[i + 2] = new_bytes[0];
                                    buf[i + 3] = new_bytes[1];
                                    changed = true;

                                    let mut text_id_log: i32 = -1;
                                    let mut text_patched = false;

                                    if let Some((texts_base, text_count, positions)) =
                                        text_layout.as_ref()
                                    {
                                        if let Some((msg_off, text_id)) =
                                            field::find_nearby_message(
                                                &buf,
                                                scan_start,
                                                scan_end,
                                                i,
                                                0xC0,
                                            )
                                        {
                                            // Primary path: grow Section1 by appending a
                                            // new dialog entry for this pickup and point
                                            // MESSAGE at it. This avoids reusing shared
                                            // text IDs so each randomized pickup can have
                                            // its own name-based line.
                                            if let Some(new_id) =
                                                field::add_dialog_entry_for_pickup(
                                                    &mut buf,
                                                    qty,
                                                    new_item_id,
                                                    false,
                                                )
                                            {
                                                let arg_off = msg_off + 2;
                                                if arg_off < buf.len() {
                                                    buf[arg_off] = new_id;
                                                    changed = true;
                                                }
                                                text_id_log = new_id as i32;
                                                text_patched = true;

                                                text_layout =
                                                    field::get_pc_field_text_layout(&buf);
                                                empty_text_slots = text_layout
                                                    .as_ref()
                                                    .map(|(
                                                        texts_base,
                                                        text_count,
                                                        positions,
                                                    )| {
                                                        field::find_empty_text_slots(
                                                            &buf,
                                                            *texts_base,
                                                            *text_count,
                                                            positions,
                                                        )
                                                    });
                                            } else {
                                                // Fallback: reuse an empty slot or patch
                                                // the original text entry in place.
                                                let original_text_id = text_id;
                                                let mut target_text_id = text_id;
                                                let mut allocated_new = false;

                                                if let Some(ref mut slots) =
                                                    empty_text_slots
                                                {
                                                    if let Some(new_id) = slots.pop() {
                                                        target_text_id = new_id;
                                                        allocated_new = true;

                                                        let arg_off = msg_off + 2;
                                                        if arg_off < buf.len() {
                                                            buf[arg_off] =
                                                                target_text_id;
                                                            changed = true;
                                                        }
                                                    }
                                                }

                                                let mut patched =
                                                    field::patch_pickup_text_in_place(
                                                        &mut buf,
                                                        *texts_base,
                                                        *text_count,
                                                        positions,
                                                        target_text_id,
                                                        qty,
                                                        new_item_id,
                                                        false,
                                                    );

                                                if !patched && allocated_new {
                                                    if let Some(ref mut slots) =
                                                        empty_text_slots
                                                    {
                                                        slots.push(target_text_id);
                                                    }

                                                    let arg_off = msg_off + 2;
                                                    if arg_off < buf.len() {
                                                        buf[arg_off] = original_text_id;
                                                        changed = true;
                                                    }

                                                    target_text_id = original_text_id;
                                                    patched =
                                                        field::patch_pickup_text_in_place(
                                                            &mut buf,
                                                            *texts_base,
                                                            *text_count,
                                                            positions,
                                                            target_text_id,
                                                            qty,
                                                            new_item_id,
                                                            false,
                                                        );
                                                }

                                                text_id_log = target_text_id as i32;
                                                text_patched = patched;
                                            }
                                        }
                                    }

                                    let display_name = lookup_inventory_name(new_item_id);
                                    field_pickups_rand_log.push_str(&format!(
                                        "field={} off=0x{:06X} old_item_id=0x{:04X} new_item_id=0x{:04X} qty={} text_id={} text_patched={} name={}\n",
                                        field_name,
                                        off,
                                        item_id,
                                        new_item_id,
                                        qty,
                                        text_id_log,
                                        text_patched,
                                        display_name,
                                    ));
                                }
                            }
                        }
                    }
                } else if opcode == 0x5B {
                    // SMTRA (Set Materia) – handle constant materia grants where
                    // both B1/B2 and B3/B4 are zero, so T is a literal materia ID
                    // and AP is a literal 3-byte value.
                    if let Some(r) = rng.as_mut() {
                        if i + 6 < buf.len() {
                            let off = i;
                            let b1b2 = buf.get(i + 1).copied().unwrap_or(0);
                            let b3b4 = buf.get(i + 2).copied().unwrap_or(0);
                            let materia_id = buf.get(i + 3).copied().unwrap_or(0);

                            // Only randomise when all bank nibbles are zero
                            // (pure constant call).
                            if b1b2 == 0 && b3b4 == 0 {
                                if field::is_field_materia_id_allowed(materia_id)
                                    && !smtra_materia_pool.contains(&materia_id)
                                {
                                    smtra_materia_pool.push(materia_id);
                                }

                                let mut new_materia_id = materia_id;
                                let pool_len = smtra_materia_pool.len();
                                if pool_len > 0 {
                                    let idx = r.gen_range(0..pool_len);
                                    new_materia_id = smtra_materia_pool[idx];

                                    if pool_len > 1 && new_materia_id == materia_id {
                                        let mut alt_idx = r.gen_range(0..(pool_len - 1));
                                        if alt_idx >= idx {
                                            alt_idx += 1;
                                        }
                                        new_materia_id = smtra_materia_pool[alt_idx];
                                    }
                                }

                                if new_materia_id != materia_id {
                                    buf[i + 3] = new_materia_id;
                                    changed = true;
                                }

                                let mut text_id_log: i32 = -1;
                                let mut text_patched = false;

                                if let Some((texts_base, text_count, positions)) =
                                    text_layout.as_ref()
                                {
                                    if let Some((msg_off, text_id)) =
                                        field::find_nearby_message(
                                            &buf,
                                            scan_start,
                                            scan_end,
                                            i,
                                            0xC0,
                                        )
                                    {
                                        // Primary path: grow Section1 by appending a new
                                        // dialog entry for this materia pickup and point
                                        // MESSAGE at it.
                                        if let Some(new_id) =
                                            field::add_dialog_entry_for_pickup(
                                                &mut buf,
                                                1,
                                                new_materia_id as u16,
                                                true,
                                            )
                                        {
                                            let arg_off = msg_off + 2;
                                            if arg_off < buf.len() {
                                                buf[arg_off] = new_id;
                                                changed = true;
                                            }
                                            text_id_log = new_id as i32;
                                            text_patched = true;

                                            text_layout =
                                                field::get_pc_field_text_layout(&buf);
                                            empty_text_slots = text_layout
                                                .as_ref()
                                                .map(|(
                                                    texts_base,
                                                    text_count,
                                                    positions,
                                                )| {
                                                    field::find_empty_text_slots(
                                                        &buf,
                                                        *texts_base,
                                                        *text_count,
                                                        positions,
                                                    )
                                                });
                                        } else {
                                            // Fallback: reuse an empty slot or patch the
                                            // original text entry in place.
                                            let original_text_id = text_id;
                                            let mut target_text_id = text_id;
                                            let mut allocated_new = false;

                                            if let Some(ref mut slots) = empty_text_slots {
                                                if let Some(new_id) = slots.pop() {
                                                    target_text_id = new_id;
                                                    allocated_new = true;

                                                    let arg_off = msg_off + 2;
                                                    if arg_off < buf.len() {
                                                        buf[arg_off] = target_text_id;
                                                        changed = true;
                                                    }
                                                }
                                            }

                                            let mut patched =
                                                field::patch_pickup_text_in_place(
                                                    &mut buf,
                                                    *texts_base,
                                                    *text_count,
                                                    positions,
                                                    target_text_id,
                                                    1,
                                                    new_materia_id as u16,
                                                    true,
                                                );

                                            if !patched && allocated_new {
                                                if let Some(ref mut slots) =
                                                    empty_text_slots
                                                {
                                                    slots.push(target_text_id);
                                                }

                                                let arg_off = msg_off + 2;
                                                if arg_off < buf.len() {
                                                    buf[arg_off] = original_text_id;
                                                    changed = true;
                                                }

                                                target_text_id = original_text_id;
                                                patched =
                                                    field::patch_pickup_text_in_place(
                                                        &mut buf,
                                                        *texts_base,
                                                        *text_count,
                                                        positions,
                                                        target_text_id,
                                                        1,
                                                        new_materia_id as u16,
                                                        true,
                                                    );
                                            }

                                            text_id_log = target_text_id as i32;
                                            text_patched = patched;
                                        }
                                    }
                                }

                                let materia_name =
                                    items::lookup_materia_name(new_materia_id);
                                field_pickups_rand_log.push_str(&format!(
                                    "field={} off=0x{:06X} old_materia_id=0x{:02X} new_materia_id=0x{:02X} text_id={} text_patched={} name={}\n",
                                    field_name,
                                    off,
                                    materia_id,
                                    new_materia_id,
                                    text_id_log,
                                    text_patched,
                                    materia_name,
                                ));
                            }
                        }
                    }
                } else if opcode == 0x82 || opcode == 0x83 || opcode == 0x84 {
                    // BITON / BITOFF / BITXOR – first, log any uses
                    // that correspond to known key-item flags so we
                    // can inspect them as potential chest-style
                    // candidates; then apply Huge Materia remapping
                    // for Var[1][66].
                    if i + 3 < scan_end {
                        let banks = buf[i + 1];
                        let bank1 = banks >> 4;
                        let bank2 = banks & 0x0F;
                        let var = buf[i + 2];
                        let pos = buf[i + 3];

                        if opcode == 0x82 && bank2 == 0 {
                            let mask: u8 = 1u8 << (pos & 7);
                            if let Some(flag) = key_item_flags
                                .iter()
                                .find(|f| f.bank == bank1 && f.addr == var && f.bit == mask)
                            {
                                let mut msg_id_log: i32 = -1;
                                let mut has_msg = false;

                                if let Some((_, text_id)) = field::find_nearby_message(
                                    &buf,
                                    scan_start,
                                    scan_end,
                                    i,
                                    0xC0,
                                ) {
                                    msg_id_log = text_id as i32;
                                    has_msg = true;
                                }

                                field_pickups_rand_log.push_str(&format!(
                                    "key_bit field={} off=0x{:06X} key_name={} bank={} addr={} bit_mask=0x{:02X} bit_index={} has_msg={} msg_id={}\n",
                                    field_name,
                                    i,
                                    flag.name,
                                    flag.bank,
                                    flag.addr,
                                    flag.bit,
                                    pos,
                                    has_msg,
                                    msg_id_log,
                                ));
                            }
                        }

                        // Huge Materia remap on Var[1][66].
                        if bank1 == 1 && bank2 == 0 && var == 66 {
                            let mut new_pos = pos;
                            for (idx, &b) in HUGE_MATERIA_BITS.iter().enumerate() {
                                if pos == b {
                                    new_pos = huge_perm[idx];
                                    break;
                                }
                            }

                            if new_pos != pos {
                                buf[i + 3] = new_pos;
                                changed = true;

                                field_pickups_rand_log.push_str(&format!(
                                    "field={} off=0x{:06X} opcode=0x{:02X} old_bit={} new_bit={}\n",
                                    field_name,
                                    i,
                                    opcode,
                                    pos,
                                    new_pos,
                                ));
                            }
                        }
                    }
                }

                let size = field::opcode_size_pc(&buf, i, scan_end);
                if size == 0 {
                    break;
                }
                i += size;
            }

            if const_itemish > 0 {
                field_index_log.push_str(&format!(
                    "field={} stitm_total={} stitm_constant={} stitm_constant_itemish={}\n",
                    field_name, total_stitm, const_stitm, const_itemish
                ));
            }

            if changed {
                if let Ok(new_payload) = field::lzs_compress(&buf) {
                    let new_lzs_size = new_payload.len() as u32;
                    let mut body = Vec::with_capacity(4 + new_payload.len());
                    body.extend_from_slice(&new_lzs_size.to_le_bytes());
                    body.extend_from_slice(&new_payload);
                    field_replacements.insert(field_name.to_ascii_lowercase(), body);
                }
            }
        }
    }

    Ok((
        field_replacements,
        md1stin_decompressed_len,
        md1stin_setword_offset,
        field_index_log,
        field_pickups_rand_log,
    ))
}

// shop structures and helpers moved to shops module

fn parse_kernel_archive(raw: &[u8]) -> Result<KernelArchive> {
    let mut files = Vec::new();
    let mut offset = 0usize;
    let mut index: u16 = 0;
    let mut prev_dir_id: Option<u16> = None;

    while offset + 6 <= raw.len() {
        let cmp_size = u16::from_le_bytes([raw[offset], raw[offset + 1]]) as usize;
        let raw_size = u16::from_le_bytes([raw[offset + 2], raw[offset + 3]]);
        let dir_id = u16::from_le_bytes([raw[offset + 4], raw[offset + 5]]);
        offset += 6;

        if offset + cmp_size > raw.len() {
            return Err(RandomiserError::Config(
                "KERNEL.BIN appears truncated while reading compressed section".to_string(),
            ));
        }

        if prev_dir_id.map_or(true, |d| d != dir_id) {
            index = 0;
            prev_dir_id = Some(dir_id);
        }

        let cmp_data = raw[offset..offset + cmp_size].to_vec();
        offset += cmp_size;

        files.push(KernelFile {
            dir_id,
            index,
            raw_size,
            cmp_data,
        });

        index = index.wrapping_add(1);
    }

    let trailer = if offset < raw.len() {
        raw[offset..].to_vec()
    } else {
        Vec::new()
    };

    Ok(KernelArchive { files, trailer })
}

fn parse_lgp_archive(raw: &[u8]) -> Result<LgpArchive> {
    if raw.len() < 16 {
        return Err(RandomiserError::Config(
            "flevel.lgp is too small to contain a valid header".to_string(),
        ));
    }

    let mut creator = [0u8; 12];
    creator.copy_from_slice(&raw[0..12]);

    let file_count = u32::from_le_bytes([raw[12], raw[13], raw[14], raw[15]]) as usize;
    let toc_start = 16usize;
    // 20-byte filename + 4-byte offset + 1 unused byte + 2-byte conflict count.
    let entry_size = 27usize;
    let toc_len = file_count
        .checked_mul(entry_size)
        .ok_or_else(|| {
            RandomiserError::Config(
                "flevel.lgp file count is unreasonably large".to_string(),
            )
        })?;

    if toc_start + toc_len > raw.len() {
        return Err(RandomiserError::Config(
            "flevel.lgp TOC extends beyond end of file".to_string(),
        ));
    }

    let mut entries = Vec::with_capacity(file_count);
    let mut offset = toc_start;
    for _ in 0..file_count {
        let entry_start = offset;
        let name_bytes = &raw[entry_start..entry_start + 20];

        let size_bytes = &raw[entry_start + 20..entry_start + 24];
        let file_offset = u32::from_le_bytes([
            raw[entry_start + 20],
            raw[entry_start + 21],
            raw[entry_start + 22],
            raw[entry_start + 23],
        ]);

        // Skip the unused byte and conflict-count word (3 bytes total).
        offset += entry_size;

        let nul_pos = name_bytes
            .iter()
            .position(|&b| b == 0)
            .unwrap_or(name_bytes.len());
        let trimmed_bytes = &name_bytes[..nul_pos];
        let name = String::from_utf8_lossy(trimmed_bytes).trim_end().to_string();

        entries.push(LgpEntry { name, offset: file_offset });
    }

    Ok(LgpArchive { creator, entries })
}

fn build_lgp_archive(
    archive: &LgpArchive,
    raw: &[u8],
    replacements: &HashMap<String, Vec<u8>>,
) -> Result<Vec<u8>> {
    let file_count = archive.entries.len();

    if file_count == 0 {
        return Ok(raw.to_vec());
    }

    // Determine where the original data blocks start (first per-file header)
    // and where they end (just after the last file's contents). Everything
    // before data_start is header/TOC/lookup/conflict data that we preserve.
    let mut data_start = usize::MAX;
    let mut old_data_end = 0usize;

    for entry in &archive.entries {
        let off = entry.offset as usize;
        if off < data_start {
            data_start = off;
        }

        if off + 24 > raw.len() {
            return Err(RandomiserError::Config(
                "flevel.lgp entry header extends beyond end of file".to_string(),
            ));
        }

        let size_bytes = &raw[off + 20..off + 24];
        let body_len = u32::from_le_bytes([
            size_bytes[0],
            size_bytes[1],
            size_bytes[2],
            size_bytes[3],
        ]) as usize;

        let end = off
            .checked_add(24)
            .and_then(|v| v.checked_add(body_len))
            .ok_or_else(|| {
                RandomiserError::Config(
                    "flevel.lgp entry size calculation overflowed".to_string(),
                )
            })?;

        if end > raw.len() {
            return Err(RandomiserError::Config(
                "flevel.lgp entry data extends beyond end of file".to_string(),
            ));
        }

        if end > old_data_end {
            old_data_end = end;
        }
    }

    if data_start == usize::MAX || data_start > raw.len() {
        return Err(RandomiserError::Config(
            "flevel.lgp has no valid data entries".to_string(),
        ));
    }

    // Start the rebuilt archive with the original header area up to
    // data_start. This includes the creator string, TOC entries, lookup
    // table and conflict data. We'll update the TOC offsets in-place below.
    let mut out = Vec::with_capacity(raw.len() + 4096);
    out.extend_from_slice(&raw[..data_start]);

    let mut new_offsets: Vec<u32> = Vec::with_capacity(file_count);
    let mut cursor = data_start;

    for entry in &archive.entries {
        let src_off = entry.offset as usize;
        let header_name_bytes = &raw[src_off..src_off + 20];

        let size_bytes = &raw[src_off + 20..src_off + 24];
        let body_len = u32::from_le_bytes([
            size_bytes[0],
            size_bytes[1],
            size_bytes[2],
            size_bytes[3],
        ]) as usize;

        let body_start = src_off + 24;
        let body_end = body_start + body_len;
        if body_end > raw.len() {
            return Err(RandomiserError::Config(
                "flevel.lgp entry body extends beyond end of file".to_string(),
            ));
        }

        let original_body = &raw[body_start..body_end];

        let key = entry.name.to_ascii_lowercase();
        let body: &[u8] = if let Some(rep) = replacements.get(&key) {
            rep.as_slice()
        } else {
            original_body
        };

        new_offsets.push(cursor as u32);

        out.extend_from_slice(header_name_bytes);
        let new_size = body.len() as u32;
        out.extend_from_slice(&new_size.to_le_bytes());
        out.extend_from_slice(body);

        cursor = out.len();
    }

    // Preserve whatever tail data existed after the last file (typically the
    // LGP terminator string and any padding).
    if old_data_end <= raw.len() {
        out.extend_from_slice(&raw[old_data_end..]);
    }

    // Finally, update the TOC offsets in the rebuilt header so that each
    // entry's offset points to its new per-file header position.
    let toc_start = 16usize;
    let entry_size = 27usize;
    let toc_len = file_count
        .checked_mul(entry_size)
        .ok_or_else(|| {
            RandomiserError::Config(
                "flevel.lgp TOC size calculation overflowed".to_string(),
            )
        })?;

    if toc_start + toc_len > out.len() {
        return Err(RandomiserError::Config(
            "rebuilt flevel.lgp header is truncated".to_string(),
        ));
    }

    for (i, new_off) in new_offsets.iter().enumerate() {
        let pos = toc_start + i * entry_size + 20;
        if pos + 4 > out.len() {
            return Err(RandomiserError::Config(
                "rebuilt flevel.lgp TOC entry extends beyond end of file".to_string(),
            ));
        }
        out[pos..pos + 4].copy_from_slice(&new_off.to_le_bytes());
    }

    Ok(out)
}

fn build_kernel_archive(archive: &KernelArchive) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    for f in &archive.files {
        // ... (rest of the code remains the same)
        let cmp_len_u16 = u16::try_from(f.cmp_data.len()).map_err(|_| {
            RandomiserError::Config(
                "Compressed KERNEL.BIN section exceeds 65535 bytes, which is unexpected".to_string(),
            )
        })?;
        out.extend_from_slice(&cmp_len_u16.to_le_bytes());
        out.extend_from_slice(&f.raw_size.to_le_bytes());
        out.extend_from_slice(&f.dir_id.to_le_bytes());
        out.extend_from_slice(&f.cmp_data);
    }

    out.extend_from_slice(&archive.trailer);

    Ok(out)
}

fn decompress_kernel_section(file: &KernelFile) -> Result<Vec<u8>> {
    let mut decoder = GzDecoder::new(file.cmp_data.as_slice());
    let mut out = Vec::with_capacity(file.raw_size as usize);
    decoder.read_to_end(&mut out)?;
    Ok(out)
}

fn compress_kernel_section(data: &[u8]) -> Result<(Vec<u8>, u16)> {
    let raw_size_u16 = u16::try_from(data.len()).map_err(|_| {
        RandomiserError::Config(
            "Decompressed KERNEL.BIN section exceeds 65535 bytes, which is unexpected".to_string(),
        )
    })?;

    let mut encoder = GzEncoder::new(Vec::new(), Compression::default());
    encoder.write_all(data)?;
    let cmp_data = encoder.finish()?;

    Ok((cmp_data, raw_size_u16))
}

fn randomize_weapon_tables(archive: &mut KernelArchive, settings: &RandomiserSettings) -> Result<()> {
    // This operates on the global weapon table (section 6). When
    // keep_weapon_appearance is true we still shuffle stats/slots/growth, but
    // we restore each weapon's original model index so visuals stay vanilla.
    if !settings.randomize_weapon_stats
        && !settings.randomize_weapon_slots
        && !settings.randomize_weapon_growth
    {
        return Ok(());
    }

    // Shuffle whole 44-byte weapon records within each weapon-data section
    // (KERNEL.BIN section 6). This safely randomises stats, slots and AP
    // growth together without needing to know individual field offsets.
    const WEAPON_RECORD_SIZE: usize = 44;
    const MODEL_INDEX_OFFSET: usize = 12;

    for file in archive.files.iter_mut() {
        // Weapon data lives in KERNEL.BIN section 6 (dir_id == 6).
        if file.dir_id != 6 {
            continue;
        }

        let mut data = decompress_kernel_section(file)?;
        if data.len() < WEAPON_RECORD_SIZE || data.len() % WEAPON_RECORD_SIZE != 0 {
            // Unexpected layout; leave this section untouched.
            continue;
        }

        let record_count = data.len() / WEAPON_RECORD_SIZE;
        if record_count <= 1 {
            continue;
        }

        let mut indices: Vec<usize> = (0..record_count).collect();
        let mut rng = StdRng::seed_from_u64(settings.seed ^ 0xDEAD_BEEF_u64 ^ (file.index as u64));
        indices.shuffle(&mut rng);

        let mut shuffled = vec![0u8; data.len()];
        for (dst_idx, &src_idx) in indices.iter().enumerate() {
            let src_off = src_idx * WEAPON_RECORD_SIZE;
            let dst_off = dst_idx * WEAPON_RECORD_SIZE;
            shuffled[dst_off..dst_off + WEAPON_RECORD_SIZE]
                .copy_from_slice(&data[src_off..src_off + WEAPON_RECORD_SIZE]);

            if settings.keep_weapon_appearance {
                let model_off = dst_off + MODEL_INDEX_OFFSET;
                if model_off < data.len() && model_off < shuffled.len() {
                    let original_model = data[model_off];
                    shuffled[model_off] = original_model;
                }
            }
        }

        let (cmp_data, raw_size) = compress_kernel_section(&shuffled)?;
        file.cmp_data = cmp_data;
        file.raw_size = raw_size;
    }

    Ok(())
}

fn weapon_class_range_for_char(char_index: usize) -> Option<(u8, u8)> {
    match char_index {
        0 => Some((0x00, 0x0F)), // Cloud - swords
        1 => Some((0x20, 0x2F)), // Barret - guns/arms
        2 => Some((0x10, 0x1F)), // Tifa - gloves
        3 => Some((0x3E, 0x48)), // Aeris - staves
        4 => Some((0x30, 0x3D)), // Red XIII - clips
        5 => Some((0x57, 0x64)), // Yuffie - shuriken
        6 => Some((0x65, 0x71)), // Cait Sith - megaphones
        7 => Some((0x72, 0x7E)), // Vincent - guns
        8 => Some((0x49, 0x56)), // Cid - spears
        _ => None,
    }
}

fn randomize_starting_equipment_and_materia(kernel_data: &mut [u8], settings: &RandomiserSettings) {
    if !settings.randomize_starting_materia
        && !settings.randomize_starting_weapons
        && !settings.randomize_starting_armor
        && !settings.randomize_starting_accessories
    {
        return;
    }

    // Character record layout (relative to the start of KERNEL.BIN section 4,
    // which is copied into the savemap at 0x0054). Each record is 0x84 bytes.
    const CHARACTER_RECORD_SIZE: usize = 0x84;
    const CLOUD_RECORD_OFFSET: usize = 0x0000; // savemap 0x0054
    const BARRET_RECORD_OFFSET: usize = 0x0084; // savemap 0x00D8

    // Within a character record, these offsets store the equipped weapon/armor
    // and their materia, as well as the equipped accessory.
    const EQUIPPED_WEAPON_OFFSET: usize = 0x1C;
    const EQUIPPED_ARMOR_OFFSET: usize = 0x1D;
    const EQUIPPED_ACCESSORY_OFFSET: usize = 0x1E;
    const WEAPON_MATERIA_OFFSET: usize = 0x40;
    const ARMOR_MATERIA_OFFSET: usize = 0x60;
    const MATERIA_SLOT_SIZE: usize = 4;
    const NUM_WEAPON_SLOTS: usize = 8;
    const NUM_ARMOR_SLOTS: usize = 8;

    // Ensure the buffer is large enough to contain at least Cloud and Barret.
    if kernel_data.len() < BARRET_RECORD_OFFSET + CHARACTER_RECORD_SIZE {
        return;
    }

    let cloud_weapon_start = CLOUD_RECORD_OFFSET + WEAPON_MATERIA_OFFSET;
    let barret_weapon_start = BARRET_RECORD_OFFSET + WEAPON_MATERIA_OFFSET;
    let cloud_armor_start = CLOUD_RECORD_OFFSET + ARMOR_MATERIA_OFFSET;
    let barret_armor_start = BARRET_RECORD_OFFSET + ARMOR_MATERIA_OFFSET;

    let weapon_region_len = NUM_WEAPON_SLOTS * MATERIA_SLOT_SIZE;
    let armor_region_len = NUM_ARMOR_SLOTS * MATERIA_SLOT_SIZE;

    // Bounds check for the materia regions.
    if barret_weapon_start + weapon_region_len > kernel_data.len()
        || cloud_weapon_start + weapon_region_len > kernel_data.len()
        || barret_armor_start + armor_region_len > kernel_data.len()
        || cloud_armor_start + armor_region_len > kernel_data.len()
    {
        return;
    }

    // Optionally randomise starting weapons/accessories for all main characters.
    let character_count = kernel_data.len() / CHARACTER_RECORD_SIZE;
    let max_chars = std::cmp::min(character_count, 9);
    if max_chars > 0 {
        let mut rng_eq = StdRng::seed_from_u64(settings.seed ^ 0x7777_1111_u64);
        for char_index in 0..max_chars {
            let record_base = char_index * CHARACTER_RECORD_SIZE;
            if settings.randomize_starting_weapons && !settings.keep_weapon_appearance {
                if let Some((start, end_incl)) = weapon_class_range_for_char(char_index) {
                    let count = end_incl.wrapping_sub(start).wrapping_add(1);
                    if count > 0 {
                        let roll = rng_eq.gen_range(0..count);
                        let new_weapon = start.wrapping_add(roll);
                        let off = record_base + EQUIPPED_WEAPON_OFFSET;
                        if off < kernel_data.len() {
                            kernel_data[off] = new_weapon;
                        }
                    }
                }
            }

            if settings.randomize_starting_armor {
                let off = record_base + EQUIPPED_ARMOR_OFFSET;
                if off < kernel_data.len() {
                    let new_armor: u8 = rng_eq.gen_range(0x00..=0x1F);
                    kernel_data[off] = new_armor;
                }
            }

            if settings.randomize_starting_accessories {
                let off = record_base + EQUIPPED_ACCESSORY_OFFSET;
                if off < kernel_data.len() {
                    // Accessory indices 0x00-0x1F are valid according to the item tables.
                    let new_acc: u8 = rng_eq.gen_range(0x00..=0x1F);
                    kernel_data[off] = new_acc;
                }
            }
        }
    }

    if !settings.randomize_starting_materia {
        return;
    }

    // Capture Barret's current materia configuration as the "empty" template.
    let mut empty_weapon = vec![0u8; weapon_region_len];
    let mut empty_armor = vec![0u8; armor_region_len];
    empty_weapon.copy_from_slice(
        &kernel_data[barret_weapon_start..barret_weapon_start + weapon_region_len],
    );
    empty_armor.copy_from_slice(
        &kernel_data[barret_armor_start..barret_armor_start + armor_region_len],
    );

    // First clear all of Cloud's weapon + armor materia slots.
    kernel_data[cloud_weapon_start..cloud_weapon_start + weapon_region_len]
        .copy_from_slice(&empty_weapon);
    kernel_data[cloud_armor_start..cloud_armor_start + armor_region_len]
        .copy_from_slice(&empty_armor);

    // Early, "safe" materia pool for starting slots. Summons should be
    // excluded here; adjust this list as you refine the config. These IDs are
    // looked up in the Save Materia List.
    const EARLY_SAFE_MATERIA_IDS: &[u8] = &[0x31, 0x35];

    let materia_pool: Vec<u8> = if settings.starting_materia_all_types {
        // Use the full 0x00..=0x5A range but skip entries that are marked as
        // unused/dummy in the item tables so we don't add blank materia.
        (0x00u8..=0x5Au8)
            .filter(|&id| {
                let name = items::lookup_materia_name(id);
                name != "(unused)" && name != "?"
            })
            .collect()
    } else {
        EARLY_SAFE_MATERIA_IDS.to_vec()
    };

    if !materia_pool.is_empty() {
        let mut rng = StdRng::seed_from_u64(settings.seed ^ 0xC1C10C1C_u64);

        // Fill Cloud's first two weapon materia slots.
        for slot_index in 0..2 {
            if slot_index >= NUM_WEAPON_SLOTS {
                break;
            }

            let slot_offset = cloud_weapon_start + slot_index * MATERIA_SLOT_SIZE;
            if slot_offset + MATERIA_SLOT_SIZE > kernel_data.len() {
                break;
            }

            let materia_index = rng.gen_range(0..materia_pool.len());
            let materia_id = materia_pool[materia_index];

            // 1 byte ID + 3 bytes AP (all zero).
            kernel_data[slot_offset] = materia_id;
            kernel_data[slot_offset + 1] = 0;
            kernel_data[slot_offset + 2] = 0;
            kernel_data[slot_offset + 3] = 0;
        }

        // Also drop 2–3 extra random materia into the Party Materia stock.
        // According to the savemap layout, Party Materia stock starts at
        // savemap offset 0x077C. Section 4 is copied into the savemap at
        // 0x0054, so within this buffer the local offset is 0x077C-0x0054 =
        // 0x0728. The block consists of 200 entries of 4 bytes each:
        //   1 byte ID + 3 bytes AP (0xFFFFFF when mastered).
        const PARTY_MATERIA_OFFSET: usize = 0x0728; // relative to section 4
        const PARTY_MATERIA_ENTRY_SIZE: usize = 4;
        const PARTY_MATERIA_SLOTS: usize = 200;

        if PARTY_MATERIA_OFFSET + PARTY_MATERIA_ENTRY_SIZE * PARTY_MATERIA_SLOTS
            <= kernel_data.len()
        {
            let extra_count = 2 + rng.gen_range(0..=1); // 2 or 3 materia
            let mut placed = 0usize;
            let mut slot = 0usize;

            while placed < extra_count && slot < PARTY_MATERIA_SLOTS {
                let entry_offset = PARTY_MATERIA_OFFSET + slot * PARTY_MATERIA_ENTRY_SIZE;

                // Treat an entry as empty if all 4 bytes are zero.
                if kernel_data[entry_offset] == 0
                    && kernel_data[entry_offset + 1] == 0
                    && kernel_data[entry_offset + 2] == 0
                    && kernel_data[entry_offset + 3] == 0
                {
                    let materia_index = rng.gen_range(0..materia_pool.len());
                    let materia_id = materia_pool[materia_index];

                    kernel_data[entry_offset] = materia_id;
                    kernel_data[entry_offset + 1] = 0;
                    kernel_data[entry_offset + 2] = 0;
                    kernel_data[entry_offset + 3] = 0;

                    placed += 1;
                }

                slot += 1;
            }
        }
    }
}

pub fn run(settings: RandomiserSettings) -> Result<()> {
    if !settings.input_path.exists() {
        return Err(RandomiserError::Config(format!(
            "Input path does not exist: {}",
            settings.input_path.display()
        )));
    }

    if !settings.output_path.exists() {
        fs::create_dir_all(&settings.output_path)?;
    }

    // All outputs for a given run go into a per-seed subfolder so that
    // multiple runs do not collide and IRO export only needs to pack
    // the files for this specific seed.
    let out_root = settings
        .output_path
        .join(format!("GoldSaucer_{}", settings.seed));
    if !out_root.exists() {
        fs::create_dir_all(&out_root)?;
    }

    let exe_src = if settings.randomize_shops {
        Some(
            find_first_existing(
                &settings.input_path,
                &["ff7_en.exe", "ff7.exe", "data/ff7_en.exe", "data/ff7.exe"],
            )
            .ok_or_else(|| {
                RandomiserError::Config(
                    "Could not find ff7.exe or ff7_en.exe under input path".to_string(),
                )
            })?,
        )
    } else {
        None
    };

    let kernel_src = find_first_existing(
        &settings.input_path,
        &["kernel/KERNEL.BIN", "lang-en/kernel/KERNEL.BIN", "data/lang-en/kernel/KERNEL.BIN"],
    )
    .ok_or_else(|| {
        RandomiserError::Config("Could not find kernel/KERNEL.BIN under input path".to_string())
    })?;

    let kernel_bytes = fs::read(&kernel_src)?;
    let mut kernel_archive = parse_kernel_archive(&kernel_bytes)?;
    let rebuilt_kernel_bytes = build_kernel_archive(&kernel_archive)?;
    let kernel_roundtrip_exact = rebuilt_kernel_bytes == kernel_bytes;

    if settings.randomize_equipment
        || settings.randomize_starting_materia
        || settings.randomize_starting_weapons
        || settings.randomize_starting_armor
        || settings.randomize_starting_accessories
        || settings.randomize_weapon_stats
        || settings.randomize_weapon_slots
        || settings.randomize_weapon_growth
    {
        if let Some(init_file) = kernel_archive
            .files
            .iter_mut()
            .find(|f| f.dir_id == 3 && f.index == 0)
        {
            let mut init_data = decompress_kernel_section(init_file)?;
            randomize_starting_equipment_and_materia(&mut init_data, &settings);
            let (cmp_data, raw_size) = compress_kernel_section(&init_data)?;
            init_file.cmp_data = cmp_data;
            init_file.raw_size = raw_size;
        }

        // When any of the weapon randomisation flags are enabled, also
        // shuffle the weapon tables so stats/slots/growth are globally
        // randomised across weapons.
        randomize_weapon_tables(&mut kernel_archive, &settings)?;
    }

    let kernel2_src = find_first_existing(
        &settings.input_path,
        &["kernel/kernel2.bin", "lang-en/kernel/kernel2.bin", "data/lang-en/kernel/kernel2.bin"],
    )
    .ok_or_else(|| {
        RandomiserError::Config("Could not find kernel/kernel2.bin under input path".to_string())
    })?;

    let scene_src = find_first_existing(
        &settings.input_path,
        &[
            "battle/scene.bin",
            "lang-en/battle/scene.bin",
            "data/battle/scene.bin",
            "data/lang-en/battle/scene.bin",
        ],
    )
    .ok_or_else(|| {
        RandomiserError::Config("Could not find battle/scene.bin under input path".to_string())
    })?;

    let flevel_src = find_first_existing(
        &settings.input_path,
        &[
            // If input is the FF7 root directory.
            "data/field/flevel.lgp",
            // If input is the "data" directory.
            "field/flevel.lgp",
            // If input is the "data/lang-en" directory (Steam default for our CLI examples).
            "../field/flevel.lgp",
        ],
    );

    let lang = "lang-en";

    let mut flevel_summary: Option<(
        PathBuf,
        PathBuf,
        usize,
        bool,
        Option<u32>,
        Option<usize>,
        bool,
        Option<usize>,
    )> = None;

    let kernel_dest = out_root
        .join("data")
        .join(lang)
        .join("kernel")
        .join("KERNEL.BIN");
    if let Some(parent) = kernel_dest.parent() {
        fs::create_dir_all(parent)?;
    }
    let new_kernel_bytes = if settings.randomize_equipment
        || settings.randomize_starting_materia
        || settings.randomize_starting_weapons
        || settings.randomize_starting_armor
        || settings.randomize_starting_accessories
        || settings.randomize_weapon_stats
        || settings.randomize_weapon_slots
        || settings.randomize_weapon_growth
    {
        build_kernel_archive(&kernel_archive)?
    } else {
        kernel_bytes.clone()
    };
    fs::write(&kernel_dest, &new_kernel_bytes)?;

    let kernel2_dest = out_root
        .join("data")
        .join(lang)
        .join("kernel")
        .join("kernel2.bin");
    if let Some(parent) = kernel2_dest.parent() {
        fs::create_dir_all(parent)?;
    }
    fs::copy(&kernel2_src, &kernel2_dest)?;

    let scene_dest = out_root
        .join("data")
        .join(lang)
        .join("battle")
        .join("scene.bin");
    if let Some(parent) = scene_dest.parent() {
        fs::create_dir_all(parent)?;
    }

    let scene_drop_summary: Option<(usize, usize)> = {
        let scene_bytes = fs::read(&scene_src)?;
        let (new_scene_bytes, summary) = randomize_scene_bin(&scene_bytes, &settings)?;
        fs::write(&scene_dest, &new_scene_bytes)?;
        summary
    };

    let flevel_dest = flevel_src.as_ref().map(|_| {
        out_root
            .join("data")
            .join("field")
            .join("flevel.lgp")
    });

    if let (Some(flevel_src), Some(flevel_dest)) = (&flevel_src, &flevel_dest) {
        let mut flevel_bytes = fs::read(flevel_src)?;
        let flevel_archive = parse_lgp_archive(&flevel_bytes)?;

        // Read creator to avoid dead_code warnings and for potential future
        // logging/diagnostics.
        let _creator_str = String::from_utf8_lossy(&flevel_archive.creator);

        let field_count = flevel_archive.entries.len();
        let md1stin_offset = flevel_archive
            .entries
            .iter()
            .find(|e| e.name.eq_ignore_ascii_case("md1stin"))
            .map(|e| e.offset);
        let has_md1stin = md1stin_offset.is_some();

        let mut md1stin_decompressed_len: Option<usize> = None;
        let mut md1stin_setword_offset: Option<usize> = None;
        let mut field_replacements: HashMap<String, Vec<u8>> = HashMap::new();

        if settings.randomize_field_pickups {
            let (
                replacements,
                md1stin_len,
                md1stin_setword,
                field_index_log,
                field_pickups_rand_log,
            ) = randomize_field_pickups_in_flevel(&flevel_bytes, &flevel_archive, &settings)?;

            field_replacements = replacements;
            md1stin_decompressed_len = md1stin_len;
            md1stin_setword_offset = md1stin_setword;

            if settings.debug {
                let index_path = out_root.join("field_stitm_index.txt");
                let _ = fs::write(&index_path, field_index_log);

                let pickups_rand_path = out_root.join("field_pickups_randomized.txt");
                let _ = fs::write(&pickups_rand_path, field_pickups_rand_log);
            }
        }

        let rebuilt_flevel_bytes =
            build_lgp_archive(&flevel_archive, &flevel_bytes, &field_replacements)?;
        let flevel_roundtrip_exact = rebuilt_flevel_bytes == flevel_bytes;

        if let Some(parent) = flevel_dest.parent() {
            fs::create_dir_all(parent)?;
        }
        fs::write(flevel_dest, &rebuilt_flevel_bytes)?;

        flevel_summary = Some((
            flevel_src.clone(),
            flevel_dest.clone(),
            field_count,
            has_md1stin,
            md1stin_offset,
            md1stin_decompressed_len,
            flevel_roundtrip_exact,
            md1stin_setword_offset,
        ));
    }

    let shops_hext_path = if settings.randomize_shops {
        if let Some(exe_src) = &exe_src {
            let exe_bytes = fs::read(exe_src)?;
            let hext = build_shops_hext(&exe_bytes, &settings)?;
            let path = out_root
                .join("hext")
                .join("ff7")
                .join("en")
                .join("shops.hext");
            if let Some(parent) = path.parent() {
                fs::create_dir_all(parent)?;
            }
            fs::write(&path, hext)?;
            Some(path)
        } else {
            None
        }
    } else {
        None
    };

    let mut log = format!("FF7 Randomiser seed: {}\n", settings.seed);
    log.push_str(&format!(
        "kernel_roundtrip_exact: {}\n",
        kernel_roundtrip_exact
    ));
    log.push_str(&format!(
        "kernel.bin: {} -> {}\n",
        kernel_src.display(),
        kernel_dest.display()
    ));
    log.push_str(&format!(
        "kernel2.bin: {} -> {}\n",
        kernel2_src.display(),
        kernel2_dest.display()
    ));
    if let Some((enemies_with_drop, total_drop_slots)) = scene_drop_summary {
        log.push_str(&format!(
            "scene.bin: {} -> {} (enemies_with_drop: {}, total_drop_slots: {})\n",
            scene_src.display(),
            scene_dest.display(),
            enemies_with_drop,
            total_drop_slots,
        ));
    } else {
        log.push_str(&format!(
            "scene.bin: {} -> {}\n",
            scene_src.display(),
            scene_dest.display()
        ));
    }

    if let Some((
        flevel_src,
        flevel_dest,
        field_count,
        has_md1stin,
        md1stin_offset,
        md1stin_decompressed_len,
        flevel_roundtrip_exact,
        md1stin_setword_offset,
    )) = &flevel_summary
    {
        let md1stin_str = if let Some(off) = md1stin_offset {
            format!("0x{:08X}", off)
        } else {
            "n/a".to_string()
        };

        let md1stin_len_str = if let Some(len) = md1stin_decompressed_len {
            format!("{} bytes", len)
        } else {
            "n/a".to_string()
        };

        let md1stin_setword_str = if let Some(off) = md1stin_setword_offset {
            format!("0x{:06X}", off)
        } else {
            "n/a".to_string()
        };

        log.push_str(&format!(
            "flevel.lgp: {} -> {} (fields: {}, md1stin_present: {}, md1stin_offset: {}, md1stin_decompressed_size: {}, md1stin_setword_offset: {}, flevel_roundtrip_exact: {})\n",
            flevel_src.display(),
            flevel_dest.display(),
            field_count,
            has_md1stin,
            md1stin_str,
            md1stin_len_str,
            md1stin_setword_str,
            flevel_roundtrip_exact,
        ));
    } else {
        log.push_str("flevel.lgp: not found under input path (skipped)\n");
    }

    if let Some(path) = &shops_hext_path {
        log.push_str(&format!("shops.hext: {}\n", path.display()));
    } else {
        log.push_str("shops.hext: not generated (shop randomisation disabled)\n");
    }

    log.push_str("key item flags by variable (bank,addr):\n");
    for ((bank, addr), flags) in items::key_item_groups_by_var() {
        log.push_str(&format!("  Var[{}][{}]:", bank, addr));
        for flag in flags {
            log.push_str(&format!(
                " {}(bit=0x{:02X}, role={:?})",
                flag.name, flag.bit, flag.role
            ));
        }
        log.push('\n');
    }

    if settings.debug {
        let log_path = out_root.join("spoiler_log.txt");
        fs::write(log_path, log)?;
    }

    Ok(())
}
