use eframe::egui;
use egui_extras::RetainedImage;
use rand::Rng;
use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::mpsc;
use std::thread;
use std::time::{Duration, Instant};

use randomiser_core::{run as run_randomiser, RandomiserSettings};

#[derive(Debug, Clone, Serialize, Deserialize)]
struct GuiConfig {
    input_path: String,
    output_path: String,
}

impl Default for GuiConfig {
    fn default() -> Self {
        Self {
            input_path: String::new(),
            output_path: String::new(),
        }
    }
}

fn config_path() -> Option<PathBuf> {
    let mut base = dirs::config_dir().or_else(|| dirs::data_dir())?;
    base.push("GoldSaucer");
    base.push("gui_config.json");
    Some(base)
}

fn load_config() -> GuiConfig {
    if let Some(path) = config_path() {
        if let Ok(data) = fs::read_to_string(&path) {
            if let Ok(cfg) = serde_json::from_str::<GuiConfig>(&data) {
                return cfg;
            }
        }
    }
    GuiConfig::default()
}

fn save_config(cfg: &GuiConfig) {
    if let Some(path) = config_path() {
        if let Some(parent) = path.parent() {
            let _ = fs::create_dir_all(parent);
        }
        if let Ok(data) = serde_json::to_string_pretty(cfg) {
            let _ = fs::write(path, data);
        }
    }
}

fn detect_ff7_install() -> Option<PathBuf> {
    // Very simple heuristics for now: common Steam / Square Enix locations.
    let candidates = [
        r"C:\Games\Steam\steamapps\common\FINAL FANTASY VII",
        r"C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VII",
        r"C:\Program Files\Steam\steamapps\common\FINAL FANTASY VII",
        r"C:\Square Soft, Inc\Final Fantasy VII",
    ];

    for c in &candidates {
        let p = Path::new(c);
        if p.exists() {
            return Some(p.to_path_buf());
        }
    }

    None
}

#[derive(Copy, Clone, Debug, Eq, PartialEq)]
enum ConfigTab {
    General,
    Field,
    Enemies,
    Shops,
    Equipment,
}

struct RandomiserApp {
    current_tab: ConfigTab,
    input_path: String,
    output_path: String,
    seed_text: String,

    logo: Option<RetainedImage>,

    randomize_enemy_drops: bool,
    randomize_enemies: bool,
    randomize_shops: bool,
    randomize_equipment: bool,
    randomize_starting_materia: bool,
    randomize_starting_weapons: bool,
    randomize_starting_accessories: bool,
    randomize_weapon_stats: bool,
    randomize_weapon_slots: bool,
    randomize_weapon_growth: bool,
    randomize_field_pickups: bool,

    is_running: bool,
    log: String,
    result_rx: Option<mpsc::Receiver<String>>,

    start_time: Option<Instant>,
    last_progress_pct: f32,
}

impl Default for RandomiserApp {
    fn default() -> Self {
        let seed = rand::thread_rng().gen::<u64>();

        let logo = RetainedImage::from_image_bytes(
            "gold_saucer_logo",
            include_bytes!("../../../GoldSaucer.png"),
        )
        .ok();

        let mut cfg = load_config();

        if cfg.input_path.is_empty() {
            if let Some(ff7) = detect_ff7_install() {
                cfg.input_path = ff7.display().to_string();
            }
        }

        if cfg.output_path.is_empty() {
            if let Some(mut base) = dirs::document_dir().or_else(|| dirs::data_dir()) {
                base.push("GoldSaucerOutput");
                cfg.output_path = base.display().to_string();
            }
        }

        Self {
            current_tab: ConfigTab::General,
            input_path: cfg.input_path,
            output_path: cfg.output_path,
            seed_text: seed.to_string(),

            logo,

            randomize_enemy_drops: true,
            randomize_enemies: true,
            randomize_shops: true,
            randomize_equipment: true,
            randomize_starting_materia: true,
            randomize_starting_weapons: true,
            randomize_starting_accessories: true,
            randomize_weapon_stats: true,
            randomize_weapon_slots: true,
            randomize_weapon_growth: true,
            randomize_field_pickups: true,

            is_running: false,
            log: String::new(),
            result_rx: None,
            start_time: None,
            last_progress_pct: 0.0,
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
                if !self.log.is_empty() {
                    self.log.push('\n');
                }
                self.log.push_str("Approx progress: 100%");
                self.is_running = false;
                self.start_time = None;
                self.last_progress_pct = 0.0;
            }
        }

        // While a run is active, emit approximate time-based progress updates.
        if self.is_running {
            if let Some(start) = self.start_time {
                let elapsed = start.elapsed().as_secs_f32();
                let est_total = 30.0_f32; // heuristic total duration in seconds
                let mut pct = (elapsed / est_total) * 100.0;
                if pct > 99.0 {
                    pct = 99.0;
                }

                if pct >= self.last_progress_pct + 5.0 {
                    self.last_progress_pct = pct;
                    if !self.log.is_empty() {
                        self.log.push('\n');
                    }
                    self.log.push_str(&format!(
                        "Approx progress: {}%",
                        pct.round() as u32
                    ));
                }
            }
        }

        egui::TopBottomPanel::bottom("footer").show(ctx, |ui| {
            ui.add_space(4.0);
            ui.horizontal(|ui| {
                ui.label("Made by Blazerwazey. GitHub Repo:");

                let link_text = egui::RichText::new("Gold Saucer")
                    .color(egui::Color32::from_rgb(80, 160, 255))
                    .underline();

                let resp = ui
                    .add(egui::Label::new(link_text).sense(egui::Sense::click()));
                if resp.clicked() {
                    ui.ctx().open_url(egui::OpenUrl::new_tab(
                        "https://github.com/blazerwazey/Gold-Saucer".to_string(),
                    ));
                }
            });
            ui.add_space(4.0);
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            if let Some(logo) = &self.logo {
                let rect = ui.max_rect();
                let tex_id = logo.texture_id(ctx);
                let uv = egui::Rect::from_min_max(
                    egui::Pos2::new(0.0, 0.0),
                    egui::Pos2::new(1.0, 1.0),
                );
                let tint = egui::Color32::from_white_alpha(40);
                ui.painter().image(tex_id, rect, uv, tint);
            }

            ui.separator();

            ui.horizontal(|ui| {
                ui.selectable_value(&mut self.current_tab, ConfigTab::General, "General");
                ui.selectable_value(&mut self.current_tab, ConfigTab::Field, "Field");
                ui.selectable_value(&mut self.current_tab, ConfigTab::Enemies, "Enemies");
                ui.selectable_value(&mut self.current_tab, ConfigTab::Shops, "Shops");
                ui.selectable_value(&mut self.current_tab, ConfigTab::Equipment, "Equipment");
            });

            ui.separator();

            match self.current_tab {
                ConfigTab::General => {
                    ui.horizontal(|ui| {
                        ui.label("Input path:");
                        ui.text_edit_singleline(&mut self.input_path);
                        if ui.button("Browse...").clicked() {
                            let mut dialog = rfd::FileDialog::new();
                            if !self.input_path.trim().is_empty() {
                                dialog = dialog.set_directory(self.input_path.trim());
                            }
                            if let Some(path) = dialog.pick_folder() {
                                self.input_path = path.display().to_string();
                            }
                        }
                    });

                    ui.horizontal(|ui| {
                        ui.label("Output path:");
                        ui.text_edit_singleline(&mut self.output_path);
                        if ui.button("Browse...").clicked() {
                            let mut dialog = rfd::FileDialog::new();
                            if !self.output_path.trim().is_empty() {
                                dialog = dialog.set_directory(self.output_path.trim());
                            }
                            if let Some(path) = dialog.pick_folder() {
                                self.output_path = path.display().to_string();
                            }
                        }
                    });

                    ui.horizontal(|ui| {
                        ui.label("Seed:");
                        ui.text_edit_singleline(&mut self.seed_text);

                        if ui.button("Random seed").clicked() {
                            let seed = rand::thread_rng().gen::<u64>();
                            self.seed_text = seed.to_string();
                        }
                    });
                }
                ConfigTab::Field => {
                    ui.label("Field randomisation:");
                    ui.checkbox(
                        &mut self.randomize_field_pickups,
                        "Randomise field pickups (chests, materia, etc.)",
                    );
                }
                ConfigTab::Enemies => {
                    ui.label("Enemy randomisation:");
                    ui.checkbox(&mut self.randomize_enemies, "Randomise enemies");
                    ui.checkbox(
                        &mut self.randomize_enemy_drops,
                        "Randomise enemy drops",
                    );
                }
                ConfigTab::Shops => {
                    ui.label("Shop randomisation:");
                    ui.checkbox(&mut self.randomize_shops, "Randomise shops");
                }
                ConfigTab::Equipment => {
                    ui.label("Equipment / starting gear:");
                    ui.checkbox(
                        &mut self.randomize_equipment,
                        "Randomise equipment (weapons/armour/accessories)",
                    );
                    ui.checkbox(
                        &mut self.randomize_starting_materia,
                        "Randomise starting materia",
                    );
                    ui.checkbox(
                        &mut self.randomize_starting_weapons,
                        "Randomise starting weapons",
                    );
                    ui.checkbox(
                        &mut self.randomize_starting_accessories,
                        "Randomise starting accessories",
                    );
                    ui.separator();
                    ui.label("Global weapon randomisation:");
                    ui.checkbox(
                        &mut self.randomize_weapon_stats,
                        "Randomise weapon stats (attack, hit, bonuses)",
                    );
                    ui.checkbox(
                        &mut self.randomize_weapon_slots,
                        "Randomise weapon materia slots",
                    );
                    ui.checkbox(
                        &mut self.randomize_weapon_growth,
                        "Randomise weapon AP growth",
                    );
                }
            }

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

                // Persist GUI config right before launching.
                save_config(&GuiConfig {
                    input_path: self.input_path.clone(),
                    output_path: self.output_path.clone(),
                });

                let settings = RandomiserSettings {
                    seed,
                    randomize_enemy_drops: self.randomize_enemy_drops,
                    randomize_enemies: self.randomize_enemies,
                    randomize_shops: self.randomize_shops,
                    randomize_equipment: self.randomize_equipment,
                    randomize_starting_materia: self.randomize_starting_materia,
                    randomize_starting_weapons: self.randomize_starting_weapons,
                    randomize_starting_accessories: self.randomize_starting_accessories,
                    randomize_weapon_stats: self.randomize_weapon_stats,
                    randomize_weapon_slots: self.randomize_weapon_slots,
                    randomize_weapon_growth: self.randomize_weapon_growth,
                    randomize_field_pickups: self.randomize_field_pickups,
                    debug: false,
                    input_path: input,
                    output_path: output,
                };

                let (tx, rx) = mpsc::channel();
                self.result_rx = Some(rx);
                self.is_running = true;
                self.start_time = Some(Instant::now());
                self.last_progress_pct = 0.0;

                if !self.log.is_empty() {
                    self.log.push('\n');
                }
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

        ctx.request_repaint_after(Duration::from_millis(100));
    }
}

fn main() -> eframe::Result<()> {
    // Load GoldSaucer.png and use it as the window/taskbar icon.
    let icon_image = image::load_from_memory(include_bytes!("../../../GoldSaucer.png"))
        .expect("Failed to load GoldSaucer.png for icon")
        .to_rgba8();
    let (icon_width, icon_height) = icon_image.dimensions();
    let icon_rgba = icon_image.into_raw();

    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_icon(egui::viewport::IconData {
            rgba: icon_rgba,
            width: icon_width,
            height: icon_height,
        }),
        ..Default::default()
    };
    eframe::run_native(
        "Gold Saucer",
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
