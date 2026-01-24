//! Query module for ada CLI
//!
//! Provides functionality to query captured trace sessions from the command line.
//! Uses bundle-first architecture: parse bundle manifest, then route to data.

mod bundle;
mod capabilities;
mod events;
mod output;
mod screenshot;
mod session;
mod transcribe;

use std::path::Path;

use anyhow::Result;

use crate::{QueryCommands, TranscribeCommands};
use bundle::Bundle;
use output::OutputFormat;

/// Run a query against a bundle
///
/// Layer 1: Open and validate the bundle manifest
/// Layer 2: Dispatch to appropriate data source based on query type
// LCOV_EXCL_START - Integration function requires real session files
pub fn run(bundle_path: &Path, cmd: QueryCommands) -> Result<()> {
    // Handle capabilities query first - doesn't need bundle
    if let QueryCommands::Capabilities { format } = &cmd {
        let fmt = parse_format(format)?;
        let caps = capabilities::Capabilities::detect();
        println!("{}", capabilities::format_capabilities(&caps, fmt));
        return Ok(());
    }

    // Layer 1: Open and validate bundle
    let bundle = Bundle::open(bundle_path)?;

    // Handle media queries that don't need trace session
    match &cmd {
        QueryCommands::Transcribe(transcribe_cmd) => {
            return execute_transcribe_query(&bundle, transcribe_cmd);
        }
        QueryCommands::Screenshot { time, output, format } => {
            let fmt = parse_format(format)?;
            let result = screenshot::extract_screenshot(&bundle, *time, output.as_deref())?;
            println!("{}", screenshot::format_screenshot(&result, fmt));
            return Ok(());
        }
        _ => {}
    }

    // Layer 2: Dispatch based on query type
    // All current queries are trace queries - need ATF data
    let session = session::Session::open(&bundle.trace_path())?;

    execute_trace_query(&session, cmd)
}

/// Execute a transcribe query
fn execute_transcribe_query(bundle: &Bundle, cmd: &TranscribeCommands) -> Result<()> {
    match cmd {
        TranscribeCommands::Info { format } => {
            let fmt = parse_format(format)?;
            let info = transcribe::get_info(bundle)?;
            println!("{}", transcribe::format_info(&info, fmt));
        }
        TranscribeCommands::Segments {
            offset,
            limit,
            since,
            until,
            format,
        } => {
            let fmt = parse_format(format)?;
            let result = transcribe::get_segments(bundle, *offset, *limit, *since, *until)?;
            println!("{}", transcribe::format_segments(&result, fmt));
        }
    }
    Ok(())
}

/// Execute a trace query against an opened session
fn execute_trace_query(session: &session::Session, cmd: QueryCommands) -> Result<()> {
    match cmd {
        QueryCommands::Summary { format } => {
            let fmt = parse_format(&format)?;
            let summary = session.summary()?;
            println!("{}", output::format_summary(&summary, fmt));
        }
        QueryCommands::Events {
            thread,
            function,
            limit,
            offset,
            since_ns,
            until_ns,
            format,
        } => {
            let fmt = parse_format(&format)?;
            let events = session.query_events(
                thread,
                function.as_deref(),
                Some(limit),
                Some(offset),
                since_ns,
                until_ns,
            )?;
            println!("{}", output::format_events(&events, session, fmt));
        }
        QueryCommands::Functions { format } => {
            let fmt = parse_format(&format)?;
            let symbols = session.list_symbols();
            println!("{}", output::format_functions(&symbols, fmt));
        }
        QueryCommands::Threads { format } => {
            let fmt = parse_format(&format)?;
            let threads = session.list_threads();
            println!("{}", output::format_threads(&threads, fmt));
        }
        QueryCommands::Calls {
            function,
            limit,
            format,
        } => {
            let fmt = parse_format(&format)?;
            let events =
                session.query_events(None, Some(&function), Some(limit), Some(0), None, None)?;
            println!("{}", output::format_events(&events, session, fmt));
        }
        QueryCommands::TimeInfo { format } => {
            let fmt = parse_format(&format)?;
            let time_info = session.time_info();
            println!("{}", output::format_time_info(&time_info, fmt));
        }
        QueryCommands::Capabilities { .. } => {
            // Already handled above before opening bundle
            unreachable!("Capabilities handled before session open")
        }
        QueryCommands::Transcribe(_) => {
            // Already handled above before opening session
            unreachable!("Transcribe handled before session open")
        }
        QueryCommands::Screenshot { .. } => {
            // Already handled above before opening session
            unreachable!("Screenshot handled before session open")
        }
    }

    Ok(())
}

/// Parse format string to OutputFormat
fn parse_format(format: &str) -> Result<OutputFormat> {
    format
        .parse()
        .map_err(|e: String| anyhow::anyhow!("{}", e))
}
// LCOV_EXCL_STOP
