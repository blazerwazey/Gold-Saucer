use clap::Parser;
use std::path::PathBuf;

use randomiser_core::{field, run, RandomiserSettings};

#[derive(Debug, Parser)]
#[command(name = "ff7-randomiser", version, about = "Final Fantasy VII randomiser tool")]
struct Args {
    #[arg(long, required_unless_present = "debug_field_lzs")]
    input: Option<PathBuf>,

    #[arg(long, required_unless_present = "debug_field_lzs")]
    output: Option<PathBuf>,

    #[arg(long, required_unless_present = "debug_field_lzs")]
    seed: Option<u64>,

    #[arg(long, default_value_t = true)]
    randomize_enemy_drops: bool,

    #[arg(long, default_value_t = true)]
    randomize_enemies: bool,

    #[arg(long, default_value_t = true)]
    randomize_shops: bool,

    #[arg(long, default_value_t = true)]
    randomize_equipment: bool,

    #[arg(long, default_value_t = true)]
    randomize_starting_materia: bool,

    #[arg(long, default_value_t = true)]
    randomize_starting_weapons: bool,

    #[arg(long, default_value_t = true)]
    randomize_starting_accessories: bool,

    #[arg(long, default_value_t = false)]
    randomize_weapon_stats: bool,

    #[arg(long, default_value_t = false)]
    randomize_weapon_slots: bool,

    #[arg(long, default_value_t = false)]
    randomize_weapon_growth: bool,

    #[arg(long, default_value_t = false)]
    randomize_field_pickups: bool,

    #[arg(long, default_value_t = false)]
    debug: bool,

    /// Debug-only: decompress a single field LZS file and dump its
    /// Section1 / vEntityScripts layout. Normal randomisation is
    /// skipped when this is provided.
    #[arg(long, value_name = "LZS", hide = true)]
    debug_field_lzs: Option<PathBuf>,
}

fn main() {
    let args = Args::parse();

    // Debug path: inspect a single field LZS and exit.
    if let Some(lzs_path) = args.debug_field_lzs.as_ref() {
        match std::fs::read(lzs_path) {
            Ok(data) => match field::lzs_decompress(&data) {
                Ok(buf) => {
                    let report = field::debug_dump_field_section1_layout(&buf);
                    println!("{}", report);
                }
                Err(e) => {
                    eprintln!("Failed to decompress {:?}: {}", lzs_path, e);
                    std::process::exit(1);
                }
            },
            Err(e) => {
                eprintln!("Failed to read {:?}: {}", lzs_path, e);
                std::process::exit(1);
            }
        }
        return;
    }

    let settings = RandomiserSettings {
        // These unwraps are safe here because clap enforces that
        // input/output/seed are present unless --debug-field-lzs was
        // provided, and we have already early-returned in that case.
        seed: args.seed.expect("seed is required unless --debug-field-lzs is used"),
        randomize_enemy_drops: args.randomize_enemy_drops,
        randomize_enemies: args.randomize_enemies,
        randomize_shops: args.randomize_shops,
        randomize_equipment: args.randomize_equipment,
        randomize_starting_materia: args.randomize_starting_materia,
        randomize_starting_weapons: args.randomize_starting_weapons,
        randomize_starting_accessories: args.randomize_starting_accessories,
        randomize_weapon_stats: args.randomize_weapon_stats,
        randomize_weapon_slots: args.randomize_weapon_slots,
        randomize_weapon_growth: args.randomize_weapon_growth,
        randomize_field_pickups: args.randomize_field_pickups,
        debug: args.debug,
        input_path: args
            .input
            .expect("input is required unless --debug-field-lzs is used"),
        output_path: args
            .output
            .expect("output is required unless --debug-field-lzs is used"),
    };

    if let Err(err) = run(settings) {
        eprintln!("Error: {err}");
        std::process::exit(1);
    }
}
