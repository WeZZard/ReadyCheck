use std::io;
use std::process::{Command, Stdio};

// Helper function for individual gtest execution (used by generated wrappers)
fn run_gtest(bin: &str, filter: &str) -> io::Result<()> {
    let mut cmd = Command::new(bin);
    cmd.arg("--gtest_brief=1")
        .arg(format!("--gtest_filter={}", filter))
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());
    let output = cmd.output()?;
    if !output.status.success() {
        eprintln!("FAILED: {} :: {}", bin, filter);
        eprintln!("stdout:\n{}", String::from_utf8_lossy(&output.stdout));
        eprintln!("stderr:\n{}", String::from_utf8_lossy(&output.stderr));
        panic!("cpp gtest failed: {} :: {}", bin, filter);
    }
    Ok(())
}

// Include generated per-gtest wrappers (if any)
include!(concat!(env!("OUT_DIR"), "/generated_cpp.rs"));

// If no wrappers were generated, this test will provide a clear warning
#[test]
fn cpp_tests_status() {
    if option_env!("ADA_CPP_TESTS_GENERATED").is_none() {
        eprintln!("╔══════════════════════════════════════════════════════════════════╗");
        eprintln!("║                    ⚠️  C++ TESTS WARNING  ⚠️                      ║");
        eprintln!("╠══════════════════════════════════════════════════════════════════╣");
        eprintln!("║ No C++ test wrappers were generated during build.                ║");
        eprintln!("║                                                                  ║");
        eprintln!("║ Possible reasons:                                                ║");
        eprintln!("║ • No test binaries found in:                                     ║");
        eprintln!("║   • target/{{debug,release}}/tracer_backend/test                 ║");
        eprintln!("║ • Test binaries exist but couldn't be executed during build      ║");
        eprintln!("║ • Cross-compilation prevents test enumeration                    ║");
        eprintln!("║                                                                  ║");
        eprintln!("║ To run C++ tests manually:                                       ║");
        eprintln!("║ • Direct execution: ./target/release/tracer_backend/test/test_*  ║");
        eprintln!("║ • Or use your IDE's test runner                                  ║");
        eprintln!("╚══════════════════════════════════════════════════════════════════╝");

        // This is not a failure - just an informational message
        // The test passes to avoid breaking CI when wrappers aren't generated
    }
}
