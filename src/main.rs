mod ui;

fn main() {
    if let Err(e) = ui::run_ui() {
        eprintln!("应用错误: {}", e);
    }
}
