use crate::host::MachineHost;
use crate::theme;
use astravm_machine::{
    AstraMachine, DISPLAY_HEIGHT, DISPLAY_WIDTH, MachineSnapshot, POST_COLS, POST_ROWS, PostState,
};
use eframe::egui::{
    self, Align, Align2, Color32, FontId, Frame, Layout, Margin, RichText, Sense, Stroke,
    StrokeKind, TextureHandle, TextureOptions, Vec2,
};
use std::time::Duration;

pub struct AstraVmApp {
    host: MachineHost,
    snapshot: MachineSnapshot,
    display_texture: Option<TextureHandle>,
    display_generation: u64,
}

impl AstraVmApp {
    pub fn new(creation_context: &eframe::CreationContext<'_>) -> Self {
        theme::install(&creation_context.egui_ctx);
        Self {
            host: MachineHost::spawn(),
            snapshot: AstraMachine::new().snapshot(),
            display_texture: None,
            display_generation: 0,
        }
    }

    fn header(&mut self, root_ui: &mut egui::Ui) {
        egui::Panel::top("instrument-header")
            .exact_size(62.0)
            .frame(
                Frame::new()
                    .fill(theme::CHASSIS)
                    .inner_margin(Margin::symmetric(20, 10))
                    .stroke(Stroke::new(1.0, theme::BORDER_SOFT)),
            )
            .show(root_ui, |ui| {
                ui.horizontal(|ui| {
                    ui.vertical(|ui| {
                        ui.add_space(1.0);
                        ui.label(
                            RichText::new("ASTRA 68")
                                .size(19.0)
                                .strong()
                                .color(theme::STARLIGHT),
                        );
                        ui.label(
                            RichText::new("REFERENCE MACHINE / ASTRAVM")
                                .size(10.0)
                                .monospace()
                                .color(theme::TEXT_TERTIARY),
                        );
                    });

                    ui.add_space(14.0);
                    status_badge(
                        ui,
                        if self.snapshot.ready_for_loader || self.snapshot.kernel_ready {
                            "POST PASS"
                        } else if self.snapshot.paused {
                            "PAUSED"
                        } else {
                            "POWER ON"
                        },
                        if self.snapshot.ready_for_loader || self.snapshot.kernel_ready {
                            theme::ION
                        } else {
                            theme::AMBER
                        },
                    );

                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if ui.button("RESET").clicked() {
                            self.host.reset();
                        }
                        let pause_label = if self.snapshot.paused {
                            "RESUME"
                        } else {
                            "PAUSE"
                        };
                        if ui.button(pause_label).clicked() {
                            self.host.set_paused(!self.snapshot.paused);
                        }
                        ui.label(
                            RichText::new("REAL ROM / MUSASHI")
                                .size(10.0)
                                .monospace()
                                .color(theme::TEXT_TERTIARY),
                        );
                    });
                });
            });
    }

    fn body(&self, root_ui: &mut egui::Ui) {
        egui::CentralPanel::default()
            .frame(
                Frame::new()
                    .fill(theme::VOID)
                    .inner_margin(Margin::same(18)),
            )
            .show(root_ui, |ui| {
                let available = ui.available_size();
                let rail_width = 310.0_f32.min(available.x * 0.32);
                let display_width = (available.x - rail_width - 16.0).max(560.0);

                ui.horizontal(|ui| {
                    ui.allocate_ui_with_layout(
                        Vec2::new(display_width, available.y),
                        Layout::top_down(Align::Min),
                        |ui| self.machine_display(ui),
                    );
                    ui.add_space(8.0);
                    ui.allocate_ui_with_layout(
                        Vec2::new(rail_width, available.y),
                        Layout::top_down(Align::Min),
                        |ui| self.post_rail(ui),
                    );
                });
            });
    }

    fn machine_display(&self, ui: &mut egui::Ui) {
        ui.horizontal(|ui| {
            ui.label(
                RichText::new("VEGA DISPLAY")
                    .size(11.0)
                    .strong()
                    .color(theme::TEXT_SECONDARY),
            );
            ui.label(
                RichText::new(format!(
                    "{} × {} / {}",
                    DISPLAY_WIDTH,
                    DISPLAY_HEIGHT,
                    if self.display_texture.is_some() {
                        "INDEX8 FRAMEBUFFER"
                    } else {
                        "ROM CONSOLE"
                    }
                ))
                .size(10.0)
                .monospace()
                .color(theme::TEXT_TERTIARY),
            );
        });
        ui.add_space(4.0);

        let maximum = Vec2::new(
            ui.available_width(),
            (ui.available_height() - 76.0).max(320.0),
        );
        let aspect = DISPLAY_WIDTH as f32 / DISPLAY_HEIGHT as f32;
        let screen_size = if maximum.x / maximum.y > aspect {
            Vec2::new(maximum.y * aspect, maximum.y)
        } else {
            Vec2::new(maximum.x, maximum.x / aspect)
        };

        ui.with_layout(Layout::top_down(Align::Center), |ui| {
            let (bezel, _) = ui.allocate_exact_size(screen_size, Sense::hover());
            let painter = ui.painter_at(bezel);
            painter.rect_filled(bezel, 3.0, theme::PANEL_RAISED);
            painter.rect_stroke(
                bezel,
                3.0,
                Stroke::new(1.0, theme::BORDER_EMPHASIS),
                StrokeKind::Inside,
            );

            let screen = bezel.shrink(9.0);
            painter.rect_filled(screen, 1.0, theme::INSET);
            painter.rect_stroke(
                screen,
                1.0,
                Stroke::new(1.0, theme::BORDER_SOFT),
                StrokeKind::Inside,
            );

            if let Some(texture) = &self.display_texture {
                painter.image(
                    texture.id(),
                    screen,
                    egui::Rect::from_min_max(egui::Pos2::ZERO, egui::pos2(1.0, 1.0)),
                    Color32::WHITE,
                );
            } else {
                let width_limited = (screen.width() - 20.0) / (POST_COLS as f32 * 0.61);
                let height_limited = (screen.height() - 18.0) / (POST_ROWS as f32 * 1.2);
                let font_size = width_limited.min(height_limited).clamp(8.0, 16.0);
                let line_height = font_size * 1.2;
                let origin = screen.left_top() + egui::vec2(10.0, 8.0);
                let font = FontId::monospace(font_size);

                for (row, text) in self.snapshot.console_rows.iter().enumerate() {
                    let color = if text.contains("POST PASS") || text.contains("READY FOR") {
                        theme::ION
                    } else if text.contains("POST") {
                        theme::STARLIGHT
                    } else {
                        Color32::from_rgb(174, 198, 201)
                    };
                    painter.text(
                        origin + egui::vec2(0.0, row as f32 * line_height),
                        Align2::LEFT_TOP,
                        text,
                        font.clone(),
                        color,
                    );
                }
            }

            for y in (screen.top() as i32..screen.bottom() as i32).step_by(4) {
                painter.line_segment(
                    [
                        egui::pos2(screen.left(), y as f32),
                        egui::pos2(screen.right(), y as f32),
                    ],
                    Stroke::new(0.5, Color32::from_rgba_unmultiplied(120, 170, 175, 8)),
                );
            }
        });

        ui.add_space(12.0);
        let cpu = format!(
            "68030 / {:.1} MHz",
            self.snapshot.cpu_hz as f64 / 1_000_000.0
        );
        let memory = format!("{} MiB SDRAM", self.snapshot.ram_bytes >> 20);
        let build = format!("0x{:08X}", self.snapshot.build_id);

        Frame::new()
            .fill(theme::CHASSIS)
            .stroke(Stroke::new(1.0, theme::BORDER_SOFT))
            .inner_margin(Margin::symmetric(12, 9))
            .show(ui, |ui| {
                ui.columns(4, |columns| {
                    ledger(&mut columns[0], "CPU", &cpu);
                    ledger(&mut columns[1], "PMMU", "INTEGRATED / MUSASHI");
                    ledger(&mut columns[2], "MEMORY", &memory);
                    ledger(&mut columns[3], "BUILD", &build);
                });
            });
    }

    fn post_rail(&self, ui: &mut egui::Ui) {
        Frame::new()
            .fill(theme::PANEL)
            .stroke(Stroke::new(1.0, theme::BORDER_SOFT))
            .inner_margin(Margin::same(14))
            .show(ui, |ui| {
                ui.label(
                    RichText::new("BOOT CONSTELLATION")
                        .size(11.0)
                        .strong()
                        .color(theme::STARLIGHT),
                );
                ui.label(
                    RichText::new("SOFTWARE-VISIBLE POWER-ON PATH")
                        .size(9.0)
                        .monospace()
                        .color(theme::TEXT_TERTIARY),
                );
                ui.add_space(10.0);

                let stage_count = self.snapshot.stages.len();
                for (index, stage) in self.snapshot.stages.iter().enumerate() {
                    let row_height = 47.0;
                    let (rect, _) = ui.allocate_exact_size(
                        Vec2::new(ui.available_width(), row_height),
                        Sense::hover(),
                    );
                    let painter = ui.painter_at(rect);
                    let node = egui::pos2(rect.left() + 8.0, rect.top() + 13.0);
                    let color = stage_color(stage.state);

                    if index + 1 < stage_count {
                        painter.line_segment(
                            [
                                node + egui::vec2(0.0, 6.0),
                                node + egui::vec2(0.0, row_height),
                            ],
                            Stroke::new(1.0, theme::BORDER_EMPHASIS),
                        );
                    }
                    painter.circle_filled(node, 4.5, color);
                    painter.circle_stroke(node, 7.0, Stroke::new(1.0, color));
                    painter.text(
                        node + egui::vec2(17.0, -8.0),
                        Align2::LEFT_TOP,
                        stage.label,
                        FontId::proportional(12.5),
                        if stage.state == PostState::Pending {
                            theme::TEXT_TERTIARY
                        } else {
                            theme::STARLIGHT
                        },
                    );
                    painter.text(
                        node + egui::vec2(17.0, 9.0),
                        Align2::LEFT_TOP,
                        stage.detail,
                        FontId::monospace(9.5),
                        theme::TEXT_TERTIARY,
                    );

                    if stage.state == PostState::Running {
                        let track = egui::Rect::from_min_size(
                            egui::pos2(rect.left() + 25.0, rect.bottom() - 5.0),
                            egui::vec2(rect.width() - 25.0, 2.0),
                        );
                        painter.rect_filled(track, 0.0, theme::BORDER_SOFT);
                        let progress = stage.progress_milli as f32 / 1000.0;
                        painter.rect_filled(
                            egui::Rect::from_min_size(
                                track.min,
                                egui::vec2(track.width() * progress, track.height()),
                            ),
                            0.0,
                            theme::AMBER,
                        );
                    }
                }

                ui.add_space(8.0);
                let (label, color) = if self.snapshot.kernel_ready {
                    ("AXIOM KERNEL READY", theme::ION)
                } else if self.snapshot.ready_for_loader {
                    ("READY FOR OS LOADER", theme::ION)
                } else if self.snapshot.paused {
                    ("CLOCK HALTED", theme::AMBER)
                } else {
                    ("POST IN PROGRESS", theme::AMBER)
                };
                status_badge(ui, label, color);
            });
    }

    fn footer(&self, root_ui: &mut egui::Ui) {
        egui::Panel::bottom("instrument-footer")
            .exact_size(31.0)
            .frame(
                Frame::new()
                    .fill(theme::CHASSIS)
                    .inner_margin(Margin::symmetric(18, 7))
                    .stroke(Stroke::new(1.0, theme::BORDER_SOFT)),
            )
            .show(root_ui, |ui| {
                ui.horizontal(|ui| {
                    ui.label(
                        RichText::new(format!("CYC {:012}", self.snapshot.cycles))
                            .size(10.0)
                            .monospace()
                            .color(theme::TEXT_SECONDARY),
                    );
                    ui.separator();
                    ui.label(
                        RichText::new(format!("PC {:08X}", self.snapshot.cpu_pc))
                            .size(10.0)
                            .monospace()
                            .color(theme::TEXT_SECONDARY),
                    );
                    ui.separator();
                    ui.label(
                        RichText::new(self.snapshot.backend)
                            .size(10.0)
                            .monospace()
                            .color(theme::TEXT_TERTIARY),
                    );
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        ui.label(
                            RichText::new("WGPU / NATIVE")
                                .size(10.0)
                                .monospace()
                                .color(theme::TEXT_TERTIARY),
                        );
                    });
                });
            });
    }
}

impl eframe::App for AstraVmApp {
    fn logic(&mut self, context: &egui::Context, _frame: &mut eframe::Frame) {
        if let Some(snapshot) = self.host.latest_snapshot() {
            if snapshot.display_generation != self.display_generation {
                self.display_generation = snapshot.display_generation;
                self.display_texture = snapshot.display_rgba.as_deref().map(|rgba| {
                    let image = egui::ColorImage::from_rgba_unmultiplied(
                        [DISPLAY_WIDTH, DISPLAY_HEIGHT],
                        rgba,
                    );
                    context.load_texture("vega-framebuffer", image, TextureOptions::NEAREST)
                });
            }
            self.snapshot = snapshot;
        }

        context.request_repaint_after(Duration::from_millis(16));
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.header(ui);
        self.footer(ui);
        self.body(ui);
    }
}

fn ledger(ui: &mut egui::Ui, label: &str, value: &str) {
    ui.vertical(|ui| {
        ui.label(
            RichText::new(label)
                .size(9.0)
                .strong()
                .color(theme::TEXT_TERTIARY),
        );
        ui.label(
            RichText::new(value)
                .size(11.0)
                .monospace()
                .color(theme::TEXT_SECONDARY),
        );
    });
}

fn stage_color(state: PostState) -> Color32 {
    match state {
        PostState::Pending => theme::TEXT_MUTED,
        PostState::Running => theme::AMBER,
        PostState::Passed => theme::ION,
        PostState::Failed => theme::FAULT,
    }
}

fn status_badge(ui: &mut egui::Ui, label: &str, color: Color32) {
    Frame::new()
        .fill(Color32::from_rgba_unmultiplied(
            color.r(),
            color.g(),
            color.b(),
            22,
        ))
        .stroke(Stroke::new(1.0, color))
        .corner_radius(2.0)
        .inner_margin(Margin::symmetric(7, 4))
        .show(ui, |ui| {
            ui.label(
                RichText::new(label)
                    .size(9.5)
                    .monospace()
                    .strong()
                    .color(color),
            );
        });
}
