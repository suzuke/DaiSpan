fn main() {
    // Only run ESP-IDF build steps when targeting espidf
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("espidf") {
        embuild::espidf::sysenv::output();

        // Copy partitions.csv to the ESP-IDF build output directory
        // so that the partition table builder can find it.
        let out_dir = std::env::var("OUT_DIR").unwrap();
        let manifest_dir = std::env::var("CARGO_MANIFEST_DIR").unwrap();
        let src = std::path::Path::new(&manifest_dir).join("partitions.csv");
        let dst = std::path::Path::new(&out_dir).join("partitions.csv");
        if src.exists() {
            std::fs::copy(&src, &dst).expect("Failed to copy partitions.csv");
        }
    }
}
