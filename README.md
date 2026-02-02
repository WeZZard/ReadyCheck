# ADA - AI Agent Debugging Architecture

A high-performance tracing system designed for debugging AI agents with minimal overhead and token-budget-aware analysis.

## Quick Start

See [Getting Started Guide](docs/GETTING_STARTED.md) for setup instructions.

## Project Overview

ADA provides a dual-lane flight recorder architecture for tracing AI agent execution:
- **Index Lane**: Always-on lightweight event capture
- **Detail Lane**: Selective persistence of rich debugging data
- **Token-Budget-Aware**: Optimized for LLM context window constraints

## Core Components

- **tracer**: Rust control plane for trace management
- **tracer_backend**: High-performance C/C++ data plane
- **query_engine**: Token-budget-aware analysis (Rust)

## Development Workflow

### Engineering Efficiency Documentation

For human developers, comprehensive guides are available:

#### Standards & Requirements

- [Engineering Standards](docs/sops/ENGINEERING_STANDARDS.md)
- [Getting Started](docs/GETTING_STARTED.md)

### For AI Agents

When using Claude Code or other AI assistants, agents will be spawned automatically based on the task. See [CLAUDE.md](CLAUDE.md) for AI-specific instructions.

## Build System

**Cargo orchestrates everything:**

```bash
cargo build --release    # Build all components
cargo test --all        # Run all tests
./utils/run_coverage.sh # Generate coverage reports
```

## Claude Code Plugin

ADA can be deployed as a Claude Code plugin for AI-assisted debugging workflows.

### Building the Plugin

```bash
# Build and package as plugin (release build, with code signing on macOS)
./utils/distribute.sh --form plugin

# Build with debug profile
./utils/distribute.sh --form plugin --debug

# Custom output directory
./utils/distribute.sh --form plugin --output /path/to/output
```

### Plugin Output Structure

```
dist/
├── .claude-plugin/
│   └── plugin.json       # Plugin metadata
├── bin/
│   ├── ada               # Main CLI (signed)
│   ├── ada-capture-daemon
│   └── ada-recorder
├── lib/
│   └── libfrida_agent.dylib
└── skills/
    ├── ada-doctor/       # System health check skill
    ├── analyze/          # Session analysis skill
    └── run/              # Application launch skill
```

### Testing the Plugin

Launch Claude Code with the distribution directory to test the plugin:

```bash
# Launch Claude Code with ADA plugin
claude --plugin ./dist

# Or specify an absolute path
claude --plugin /absolute/path/to/dist
```

### Available Skills

Once launched with the plugin, you can use ADA skills:

- `/ada:run` - Launch an application with ADA tracing
- `/ada:analyze` - Analyze a captured session
- `/ada:ada-doctor` - Check system dependencies

## Quality Requirements

- **Build Success**: 100% (all components must build)
- **Test Success**: 100% (all tests must pass)
- **Coverage**: 100% on changed lines
- **Integration Score**: 100/100

## Platform Requirements

### macOS
- Apple Developer Certificate ($99/year) required for tracing
- See [Platform Constraints](docs/definitions/constraints/) for details

### Linux
- May require ptrace capabilities
- See platform-specific setup in Getting Started guide

## Documentation Structure

```
docs/
├── definitions/             # MEI-supporting artifacts
│   ├── behaviors/          # BDD behaviors (BH-XXX)
│   ├── constraints/        # System constraints (CN-XXX)
│   ├── environments/       # Environment specs (EV-XXX)
│   ├── user_stories/       # User stories (US-XXX)
│   ├── personas/           # User personas (PS-XXX)
│   └── enablers/           # Technical enablers (EN-XXX)
├── design/                  # Design documentation
│   └── architecture/       # System architecture
├── business/               # Business analysis
├── proposals/              # Design proposals (PP-XXXX)
├── sops/                   # Standard operating procedures
└── progress_trackings/     # M/E/I tracking artifacts
```

## Contributing

This project follows a strict TDD workflow with mandatory quality gates. All contributions must:
1. Pass 100% of tests
2. Achieve 100% coverage on changed lines
3. Follow existing patterns and conventions
4. Include appropriate documentation

## License

[License information to be added]

## Support

For issues or questions, please refer to the documentation or create an issue in the repository.
## Quality Metrics

![Quality Gate](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/integration-score.json)
![Rust Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/rust-coverage.json)
![C/C++ Coverage](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/agentic-infra/ADA/main/.github/badges/cpp-coverage.json)
![Build Status](https://github.com/agentic-infra/ADA/workflows/Quality%20Gate%20CI/badge.svg)

