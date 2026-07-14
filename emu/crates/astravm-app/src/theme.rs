use eframe::egui::{self, Color32, FontFamily, FontId, Stroke, TextStyle};

pub const VOID: Color32 = Color32::from_rgb(8, 11, 15);
pub const CHASSIS: Color32 = Color32::from_rgb(13, 17, 23);
pub const PANEL: Color32 = Color32::from_rgb(17, 23, 31);
pub const PANEL_RAISED: Color32 = Color32::from_rgb(21, 28, 37);
pub const INSET: Color32 = Color32::from_rgb(5, 8, 11);
pub const BORDER_SOFT: Color32 = Color32::from_rgb(38, 48, 59);
pub const BORDER_EMPHASIS: Color32 = Color32::from_rgb(57, 72, 84);

pub const STARLIGHT: Color32 = Color32::from_rgb(216, 225, 232);
pub const TEXT_SECONDARY: Color32 = Color32::from_rgb(157, 172, 184);
pub const TEXT_TERTIARY: Color32 = Color32::from_rgb(111, 126, 139);
pub const TEXT_MUTED: Color32 = Color32::from_rgb(73, 87, 99);

pub const ION: Color32 = Color32::from_rgb(105, 209, 216);
pub const ION_DIM: Color32 = Color32::from_rgb(37, 87, 94);
pub const AMBER: Color32 = Color32::from_rgb(205, 164, 91);
pub const FAULT: Color32 = Color32::from_rgb(211, 104, 100);

pub fn install(context: &egui::Context) {
    let mut style = (*context.style_of(egui::Theme::Dark)).clone();
    style.spacing.item_spacing = egui::vec2(8.0, 8.0);
    style.spacing.button_padding = egui::vec2(12.0, 7.0);
    style.spacing.indent = 16.0;

    style.text_styles.insert(
        TextStyle::Heading,
        FontId::new(20.0, FontFamily::Proportional),
    );
    style
        .text_styles
        .insert(TextStyle::Body, FontId::new(14.0, FontFamily::Proportional));
    style.text_styles.insert(
        TextStyle::Button,
        FontId::new(13.0, FontFamily::Proportional),
    );
    style.text_styles.insert(
        TextStyle::Monospace,
        FontId::new(13.0, FontFamily::Monospace),
    );
    style.text_styles.insert(
        TextStyle::Small,
        FontId::new(11.0, FontFamily::Proportional),
    );

    let mut visuals = egui::Visuals::dark();
    visuals.override_text_color = Some(STARLIGHT);
    visuals.panel_fill = CHASSIS;
    visuals.window_fill = PANEL;
    visuals.extreme_bg_color = INSET;
    visuals.faint_bg_color = PANEL;
    visuals.selection.bg_fill = ION_DIM;
    visuals.selection.stroke = Stroke::new(1.0, ION);
    visuals.hyperlink_color = ION;
    visuals.warn_fg_color = AMBER;
    visuals.error_fg_color = FAULT;

    visuals.widgets.noninteractive.bg_fill = PANEL;
    visuals.widgets.noninteractive.weak_bg_fill = PANEL;
    visuals.widgets.noninteractive.bg_stroke = Stroke::new(1.0, BORDER_SOFT);
    visuals.widgets.noninteractive.fg_stroke = Stroke::new(1.0, TEXT_SECONDARY);
    visuals.widgets.inactive.bg_fill = PANEL_RAISED;
    visuals.widgets.inactive.weak_bg_fill = PANEL_RAISED;
    visuals.widgets.inactive.bg_stroke = Stroke::new(1.0, BORDER_SOFT);
    visuals.widgets.inactive.fg_stroke = Stroke::new(1.0, STARLIGHT);
    visuals.widgets.hovered.bg_fill = Color32::from_rgb(25, 35, 45);
    visuals.widgets.hovered.weak_bg_fill = Color32::from_rgb(25, 35, 45);
    visuals.widgets.hovered.bg_stroke = Stroke::new(1.0, BORDER_EMPHASIS);
    visuals.widgets.hovered.fg_stroke = Stroke::new(1.0, STARLIGHT);
    visuals.widgets.active.bg_fill = ION_DIM;
    visuals.widgets.active.weak_bg_fill = ION_DIM;
    visuals.widgets.active.bg_stroke = Stroke::new(1.0, ION);
    visuals.widgets.active.fg_stroke = Stroke::new(1.0, STARLIGHT);

    style.visuals = visuals;
    context.set_style_of(egui::Theme::Dark, style);
    context.set_theme(egui::Theme::Dark);
}
