use clap::Parser;
use std::path::PathBuf;

use randomiser_core::{run, RandomiserSettings};

#[derive(Debug, Parser)]
#[command(name = "ff7-randomiser", version, about = "Final Fantasy VII randomiser tool")]
struct Args {
    #[arg(long)]
    input: PathBuf,

    #[arg(long)]
    output: PathBuf,

    #[arg(long)]
    seed: u64,

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

    #[arg(long, default_value_t = false)]
    randomize_field_pickups: bool,
}

fn main() {
    let args = Args::parse();

    let settings = RandomiserSettings {
        seed: args.seed,
        randomize_enemy_drops: args.randomize_enemy_drops,
        randomize_enemies: args.randomize_enemies,
        randomize_shops: args.randomize_shops,
        randomize_equipment: args.randomize_equipment,
        randomize_starting_materia: args.randomize_starting_materia,
        randomize_starting_weapons: args.randomize_starting_weapons,
        randomize_field_pickups: args.randomize_field_pickups,
        input_path: args.input,
        output_path: args.output,
    };

    if let Err(err) = run(settings) {
        eprintln!("Error: {err}");
        std::process::exit(1);
    }
}
