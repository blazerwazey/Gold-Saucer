use eframe::egui;
use rand::Rng;
use std::path::PathBuf;
use std::sync::mpsc;
use std::thread;

use randomiser_core::{run as run_randomiser, RandomiserSettings};

struct RandomiserApp {
    input_path: String,
    output_path: String,
    seed_text: String,

    randomize_enemy_drops: bool,
    randomize_enemies: bool,
    randomize_shops: bool,
    randomize_equipment: bool,
    randomize_starting_materia: bool,
    randomize_starting_weapons: bool,
    randomize_field_pickups: bool,

    is_running: bool,
    log: String,
    result_rx: Option<mpsc::Receiver<String>>,
}

impl Default for RandomiserApp {
    fn default() -> Self {
        let seed = rand::thread_rng().gen::<u64>();

        Self {
            input_path: String::new(),
            output_path: String::new(),
            seed_text: seed.to_string(),

            randomize_enemy_drops: true,
            randomize_enemies: true,
            randomize_shops: true,
            randomize_equipment: true,
            randomize_starting_materia: true,
            randomize_starting_weapons: true,
            randomize_field_pickups: true,

            is_running: false,
            log: String::new(),
            result_rx: None,
        }
    }
}

impl eframe::App for RandomiserApp {
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        if let Some(rx) = self.result_rx.as_ref() {
            while let Ok(msg) = rx.try_recv() {
                if !self.log.is_empty() {
                    self.log.push('\n');
                }
                self.log.push_str(&msg);
                self.is_running = false;
            }
        }

        egui::CentralPanel::default().show(ctx, |ui| {
            ui.heading("FF7 Randomiser GUI");

            ui.separator();

            ui.horizontal(|ui| {
                ui.label("Input path:");
                ui.text_edit_singleline(&mut self.input_path);
            });

            ui.horizontal(|ui| {
                ui.label("Output path:");
                ui.text_edit_singleline(&mut self.output_path);
            });

            ui.horizontal(|ui| {
                ui.label("Seed:");
                ui.text_edit_singleline(&mut self.seed_text);

                if ui.button("Random seed").clicked() {
                    let seed = rand::thread_rng().gen::<u64>();
                    self.seed_text = seed.to_string();
                }
            });

            ui.separator();
            ui.label("Randomisation options:");

            ui.columns(2, |cols| {
                cols[0].checkbox(&mut self.randomize_field_pickups, "Field pickups");
                cols[0].checkbox(&mut self.randomize_enemies, "Enemies");
                cols[0].checkbox(&mut self.randomize_enemy_drops, "Enemy drops");

                cols[1].checkbox(&mut self.randomize_shops, "Shops");
                cols[1].checkbox(&mut self.randomize_equipment, "Equipment");
                cols[1].checkbox(&mut self.randomize_starting_materia, "Starting materia");
                cols[1].checkbox(&mut self.randomize_starting_weapons, "Starting weapons");
            });

            ui.separator();

            let run_button_enabled = !self.is_running;
            if ui.add_enabled(run_button_enabled, egui::Button::new("Run randomiser")).clicked() {
                let seed = self
                    .seed_text
                    .trim()
                    .parse::<u64>()
                    .unwrap_or_else(|_| rand::thread_rng().gen::<u64>());

                let input = PathBuf::from(self.input_path.trim());
                let output = PathBuf::from(self.output_path.trim());

                let settings = RandomiserSettings {
                    seed,
                    randomize_enemy_drops: self.randomize_enemy_drops,
                    randomize_enemies: self.randomize_enemies,
                    randomize_shops: self.randomize_shops,
                    randomize_equipment: self.randomize_equipment,
                    randomize_starting_materia: self.randomize_starting_materia,
                    randomize_starting_weapons: self.randomize_starting_weapons,
                    randomize_field_pickups: self.randomize_field_pickups,
                    input_path: input,
                    output_path: output,
                };

                let (tx, rx) = mpsc::channel();
                self.result_rx = Some(rx);
                self.is_running = true;

                self.log.push_str(&format!(
                    "Starting randomiser with seed {}...",
                    seed
                ));

                thread::spawn(move || {
                    let message = match run_randomiser(settings) {
                        Ok(()) => "Randomiser finished successfully.".to_string(),
                        Err(e) => format!("Randomiser error: {}", e),
                    };

                    let _ = tx.send(message);
                });
            }

            ui.separator();
            ui.label("Log:");
            egui::ScrollArea::vertical()
                .id_source("log_scroll")
                .show(ui, |ui| {
                    ui.monospace(&self.log);
                });
        });

        ctx.request_repaint_after(std::time::Duration::from_millis(100));
    }
}

fn main() -> eframe::Result<()> {
    let native_options = eframe::NativeOptions::default();
    eframe::run_native(
        "FF7 Randomiser GUI",
        native_options,
        Box::new(|cc| {
            // Start from the dark visuals so we keep the black background.
            cc.egui_ctx.set_visuals(egui::Visuals::dark());

            // Ensure default fonts are installed.
            let fonts = egui::FontDefinitions::default();
            cc.egui_ctx.set_fonts(fonts);

            // Force a bright text color so labels are clearly visible.
            let mut style = (*cc.egui_ctx.style()).clone();
            style
                .visuals
                .override_text_color = Some(egui::Color32::from_rgb(240, 240, 240));
            cc.egui_ctx.set_style(style);

            Box::new(RandomiserApp::default())
        }),
    )
}
