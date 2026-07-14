mod app;
mod host;
mod theme;

use app::AstraVmApp;
use eframe::egui;

fn main() -> eframe::Result {
    let options = eframe::NativeOptions {
        renderer: eframe::Renderer::Wgpu,
        viewport: egui::ViewportBuilder::default()
            .with_title("AstraVM — Astra 68 Reference Machine")
            .with_inner_size([1280.0, 820.0])
            .with_min_inner_size([980.0, 680.0]),
        ..Default::default()
    };

    eframe::run_native(
        "AstraVM",
        options,
        Box::new(|creation_context| Ok(Box::new(AstraVmApp::new(creation_context)))),
    )
}
