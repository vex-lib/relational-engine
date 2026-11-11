//! Build only the engine-owned C search primitive; never imported comparison code.
//! No dependency downloads. CC/AR can select a cross toolchain explicitly.
use std::{env, path::PathBuf, process::Command};

fn main() {
    println!("cargo:rerun-if-changed=../search/primitives/name_search.c");
    println!("cargo:rerun-if-changed=../search/primitives/name_search.h");
    println!("cargo:rerun-if-changed=../../util/throw.h");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");
    if env::var("CARGO_CFG_TARGET_ENV").as_deref() == Ok("msvc") {
        panic!("C search MSVC build is not implemented; use a supported clang/GNU toolchain");
    }
    if env::var("HOST").unwrap() != env::var("TARGET").unwrap()
        && (env::var_os("CC").is_none() || env::var_os("AR").is_none()) {
        panic!("cross builds require an explicit CC and AR toolchain");
    }
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let object = out.join("name_search.o");
    let mut compiler = Command::new(env::var_os("CC").unwrap_or_else(|| "clang".into()));
    compiler.args(["-std=c23", "-Wall", "-Wextra", "-Werror", "-O2", "-I..", "-I../..", "-I../../../lib", "-I../../vexspoke"]);
    if env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("macos") {
        compiler.args(["-arch", "arm64", "-mcpu=apple-m1", "-mmacosx-version-min=14.0"]);
    }
    assert!(compiler.args(["-c", "../search/primitives/name_search.c", "-o"])
        .arg(&object).status().expect("C compiler launch").success(), "C search compilation failed");
    let library = out.join("libre_name_search.a");
    assert!(Command::new(env::var_os("AR").unwrap_or_else(|| "ar".into()))
        .arg("crs").arg(&library).arg(&object).status().expect("archiver launch").success(), "C search archive failed");
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=re_name_search");
}
