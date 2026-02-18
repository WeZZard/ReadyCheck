//! Capability detection for query features
//!
//! Detects available query commands and their tool requirements.

use serde::Serialize;

use super::output::OutputFormat;

/// A query capability with its availability status
#[derive(Debug, Clone, Serialize)]
pub struct QueryCapability {
    /// Whether this capability is available
    pub available: bool,
    /// External tool required (if any)
    #[serde(skip_serializing_if = "Option::is_none")]
    pub requires: Option<String>,
    /// Installation hint if tool is missing
    #[serde(skip_serializing_if = "Option::is_none")]
    pub install_hint: Option<String>,
}

/// All query capabilities
#[derive(Debug, Clone, Serialize)]
pub struct Capabilities {
    pub summary: QueryCapability,
    pub events: QueryCapability,
    pub functions: QueryCapability,
    pub threads: QueryCapability,
    pub calls: QueryCapability,
    pub time_info: QueryCapability,
    pub transcribe: QueryCapability,
    pub screenshot: QueryCapability,
}

impl Capabilities {
    /// Detect all query capabilities
    pub fn detect() -> Self {
        Capabilities {
            summary: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            events: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            functions: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            threads: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            calls: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            time_info: QueryCapability {
                available: true,
                requires: None,
                install_hint: None,
            },
            transcribe: detect_whisper_capability(),
            screenshot: detect_ffmpeg_capability(),
        }
    }
}

/// Check if whisper is available (bundled or system)
fn detect_whisper_capability() -> QueryCapability {
    let available = ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::WhisperCpp);

    QueryCapability {
        available,
        requires: Some("whisper".to_string()),
        install_hint: Some("Run: ./utils/init_media_tools.sh".to_string()),
    }
}

/// Check if ffmpeg is available (bundled or system)
fn detect_ffmpeg_capability() -> QueryCapability {
    let available = ada_cli::binary_resolver::is_available(ada_cli::binary_resolver::Tool::Ffmpeg);

    QueryCapability {
        available,
        requires: Some("ffmpeg".to_string()),
        install_hint: Some("Run: ./utils/init_media_tools.sh".to_string()),
    }
}

/// Format capabilities output
pub fn format_capabilities(caps: &Capabilities, format: OutputFormat) -> String {
    match format {
        OutputFormat::Text | OutputFormat::Line => format_capabilities_text(caps),
        OutputFormat::Json => format_capabilities_json(caps),
    }
}

fn format_capabilities_text(caps: &Capabilities) -> String {
    let mut output = String::new();
    output.push_str("Available Queries:\n\n");

    let queries = [
        ("summary", &caps.summary),
        ("events", &caps.events),
        ("functions", &caps.functions),
        ("threads", &caps.threads),
        ("calls", &caps.calls),
        ("time-info", &caps.time_info),
        ("transcribe", &caps.transcribe),
        ("screenshot", &caps.screenshot),
    ];

    for (name, cap) in queries {
        let status = if cap.available { "available" } else { "not available" };
        let requires = cap
            .requires
            .as_ref()
            .map(|r| format!(" (requires: {})", r))
            .unwrap_or_default();
        let hint = if !cap.available {
            cap.install_hint
                .as_ref()
                .map(|h| format!("\n             Install: {}", h))
                .unwrap_or_default()
        } else {
            String::new()
        };

        output.push_str(&format!("  {:<12} {}{}{}\n", name, status, requires, hint));
    }

    output
}

fn format_capabilities_json(caps: &Capabilities) -> String {
    #[derive(Serialize)]
    struct JsonCapabilities {
        queries: std::collections::HashMap<String, QueryCapability>,
    }

    let mut queries = std::collections::HashMap::new();
    queries.insert("summary".to_string(), caps.summary.clone());
    queries.insert("events".to_string(), caps.events.clone());
    queries.insert("functions".to_string(), caps.functions.clone());
    queries.insert("threads".to_string(), caps.threads.clone());
    queries.insert("calls".to_string(), caps.calls.clone());
    queries.insert("time-info".to_string(), caps.time_info.clone());
    queries.insert("transcribe".to_string(), caps.transcribe.clone());
    queries.insert("screenshot".to_string(), caps.screenshot.clone());

    let json = JsonCapabilities { queries };
    serde_json::to_string_pretty(&json).unwrap_or_else(|_| "{}".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_capabilities__detect__then_built_in_always_available() {
        let caps = Capabilities::detect();
        assert!(caps.summary.available);
        assert!(caps.events.available);
        assert!(caps.functions.available);
        assert!(caps.threads.available);
        assert!(caps.calls.available);
        assert!(caps.time_info.available);
    }

    #[test]
    fn test_capabilities__transcribe__then_has_requirements() {
        let caps = Capabilities::detect();
        assert_eq!(caps.transcribe.requires, Some("whisper".to_string()));
        assert!(caps.transcribe.install_hint.is_some());
    }

    #[test]
    fn test_capabilities__screenshot__then_has_requirements() {
        let caps = Capabilities::detect();
        assert_eq!(caps.screenshot.requires, Some("ffmpeg".to_string()));
        assert!(caps.screenshot.install_hint.is_some());
    }

    #[test]
    fn test_format_capabilities_text__then_lists_all() {
        let caps = Capabilities::detect();
        let output = format_capabilities(&caps, OutputFormat::Text);
        assert!(output.contains("summary"));
        assert!(output.contains("events"));
        assert!(output.contains("transcribe"));
        assert!(output.contains("screenshot"));
    }

    #[test]
    fn test_format_capabilities_json__then_valid_json() {
        let caps = Capabilities::detect();
        let output = format_capabilities(&caps, OutputFormat::Json);
        let parsed: serde_json::Value = serde_json::from_str(&output).unwrap();
        assert!(parsed["queries"]["summary"]["available"].as_bool().unwrap());
    }
}
