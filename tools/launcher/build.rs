use std::path::Path;

fn main() {
    // Bonesaw.version is the single source of truth for the shipped version number.
    let version_file = Path::new("../client-update/Bonesaw.version");
    println!("cargo:rerun-if-changed=../client-update/Bonesaw.version");
    let version = std::fs::read_to_string(version_file)
        .unwrap_or_else(|e| panic!("cannot read {}: {e}", version_file.display()));
    let version = version.trim();
    if version.is_empty() {
        panic!("Bonesaw.version is empty");
    }
    println!("cargo:rustc-env=BONESAW_VERSION={version}");

    for name in ["patch-Y.MPQ", "patch-enUS-4.MPQ"] {
        let p = Path::new("payload").join(name);
        println!("cargo:rerun-if-changed=payload/{name}");
        if !p.exists() {
            panic!(
                "missing payload/{name}. Run: python tools/launcher/build_launcher.py \
                 (it copies tools/client-patch/dist into tools/launcher/payload)"
            );
        }
    }

    // Only declare the icon dependency when the file exists: cargo treats a
    // rerun-if-changed on a missing path as "always dirty", which would rebuild
    // the exe on every invocation. The binary is not byte-reproducible, so a
    // needless rebuild invalidates a hash that may already be published.
    let icon = Path::new("assets/bonesaw.ico");
    if icon.exists() {
        println!("cargo:rerun-if-changed=assets/bonesaw.ico");
    }
    let mut res = winresource::WindowsResource::new();
    res.set("ProductName", "Bonesaw")
        .set("FileDescription", "Bonesaw launcher")
        .set("CompanyName", "Bonesaw")
        .set("LegalCopyright", "")
        .set("OriginalFilename", "Bonesaw.exe")
        .set("FileVersion", version)
        .set("ProductVersion", version);
    if icon.exists() {
        res.set_icon("assets/bonesaw.ico");
    }
    if let Err(e) = res.compile() {
        println!("cargo:warning=resource compile skipped: {e}");
    }
}
