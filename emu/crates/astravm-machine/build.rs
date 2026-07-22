use std::env;
use std::path::{Path, PathBuf};
use std::process::Command;

fn main() {
    let manifest_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let repository = manifest_dir.join("../../..");
    let musashi = repository.join("third_party/musashi");
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());

    let generator_source = musashi.join("m68kmake.c");
    let generator = out_dir.join(format!("m68kmake{}", env::consts::EXE_SUFFIX));
    let compiler = cc::Build::new().get_compiler();
    let status = compiler
        .to_command()
        .arg(&generator_source)
        .arg("-o")
        .arg(&generator)
        .status()
        .expect("failed to run the C compiler for Musashi's opcode generator");
    assert!(
        status.success(),
        "failed to build Musashi's opcode generator"
    );

    let status = Command::new(&generator)
        .arg(&out_dir)
        .arg(musashi.join("m68k_in.c"))
        .status()
        .expect("failed to run Musashi's opcode generator");
    assert!(status.success(), "Musashi opcode generation failed");

    let sources = [
        musashi.join("m68kcpu.c"),
        musashi.join("m68kdasm.c"),
        musashi.join("softfloat/softfloat.c"),
        musashi.join("astra/pmmu030.c"),
        out_dir.join("m68kops.c"),
    ];

    let mut build = cc::Build::new();
    build
        .include(&musashi)
        .include(&out_dir)
        .define("M68K_EMULATE_INT_ACK", "M68K_OPT_ON")
        .flag_if_supported("-fno-common")
        .warnings(false)
        .files(&sources)
        .compile("astravm_musashi");

    if env::var("CARGO_CFG_TARGET_FAMILY").as_deref() == Ok("unix") {
        println!("cargo:rustc-link-lib=m");
    }

    rerun_if_changed(&generator_source);
    for source in sources.iter().take(4) {
        rerun_if_changed(source);
    }
    for header in [
        "m68k.h",
        "m68kconf.h",
        "m68kcpu.h",
        "m68kmmu.h",
        "m68k_in.c",
        "m68kfpu.c",
        "softfloat/softfloat.h",
        "astra/pmmu030.h",
    ] {
        rerun_if_changed(&musashi.join(header));
    }
}

fn rerun_if_changed(path: &Path) {
    println!("cargo:rerun-if-changed={}", path.display());
}
