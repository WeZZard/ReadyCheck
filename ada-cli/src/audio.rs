//! Audio utilities for preprocessing voice recordings.

use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{bail, Context, Result};

/// Target sample rate for whisper-cli input.
pub const WHISPER_SAMPLE_RATE: u32 = 16_000;

/// Ensure a voice file is 16 kHz mono WAV for whisper-cli.
///
/// Returns the original path if already 16 kHz, otherwise resamples into
/// `temp_dir` and returns the resampled path.
pub fn ensure_16khz(voice_path: &Path, temp_dir: &Path) -> Result<PathBuf> {
    let ffprobe = crate::binary_resolver::resolve(crate::binary_resolver::Tool::Ffprobe)
        .map_err(|_| anyhow::anyhow!("ffprobe not available. Run: ./utils/init_media_tools.sh"))?;

    // Probe the sample rate
    let probe_output = Command::new(&ffprobe)
        .args(["-v", "error", "-select_streams", "a:0"])
        .args(["-show_entries", "stream=sample_rate"])
        .args(["-of", "default=noprint_wrappers=1:nokey=1"])
        .arg(voice_path)
        .output()
        .with_context(|| "Failed to run ffprobe")?;

    if !probe_output.status.success() {
        bail!(
            "ffprobe failed: {}",
            String::from_utf8_lossy(&probe_output.stderr)
        );
    }

    let rate_str = String::from_utf8_lossy(&probe_output.stdout);
    let sample_rate: u32 = rate_str.trim().parse().unwrap_or(0);

    if sample_rate == WHISPER_SAMPLE_RATE {
        return Ok(voice_path.to_path_buf());
    }

    // Resample to 16 kHz mono WAV
    let ffmpeg = crate::binary_resolver::resolve(crate::binary_resolver::Tool::Ffmpeg)
        .map_err(|_| anyhow::anyhow!("ffmpeg not available. Run: ./utils/init_media_tools.sh"))?;

    let resampled = temp_dir.join("voice_16k.wav");
    let output = Command::new(&ffmpeg)
        .args(["-y", "-i"])
        .arg(voice_path)
        .args(["-ar", "16000", "-ac", "1"])
        .arg(&resampled)
        .output()
        .with_context(|| "Failed to resample audio with ffmpeg")?;

    if !output.status.success() {
        bail!(
            "ffmpeg resample failed: {}",
            String::from_utf8_lossy(&output.stderr)
        );
    }

    Ok(resampled)
}
