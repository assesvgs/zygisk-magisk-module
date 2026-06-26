mod ipc;
mod process;
mod server;

fn main() {
    android_logger::init_once(
        android_logger::Config::default()
            .with_max_level(log::LevelFilter::Debug)
            .with_tag("Zygisk"),
    );

    log::info!("zygiskd starting");

    let args: Vec<String> = std::env::args().collect();
    match args.get(1).map(String::as_str) {
        Some("daemon") | None => {
            log::info!("zygiskd daemon mode");
            server::run();
        }
        _ => {
            log::error!("Usage: zygiskd [daemon]");
            std::process::exit(1);
        }
    }
}
