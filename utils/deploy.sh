#!/bin/bash
# Deploy ADA skills to Claude Code
#
# For plugin form: Creates distribution in ./dist with local testing instructions
# For standalone form: Deploys to ~/.claude/commands/ or <project>/.claude/commands/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

FORM=""
SCOPE=""
PROJECT_PATH=""

usage() {
    echo "Usage: $0 --form <plugin|standalone> [--scope <user|project>] [<project_path>]"
    echo ""
    echo "Deploy ADA skills to Claude Code."
    echo ""
    echo "Options:"
    echo "  --form <plugin|standalone>  Required. Distribution form:"
    echo "                                standalone - Deploy as standalone skills"
    echo "                                plugin     - Create plugin distribution for local testing"
    echo "  --scope <user|project>      Required for standalone. Deployment scope:"
    echo "                                user    - Deploy to user scope (~/.claude/)"
    echo "                                project - Deploy to project scope (requires project_path)"
    echo "  <project_path>              Required when --scope project. Path to the project directory."
    echo ""
    echo "Examples:"
    echo "  $0 --form plugin                              # Create plugin for local testing"
    echo "  $0 --form standalone --scope user             # Deploy standalone to user scope"
    echo "  $0 --form standalone --scope project ./myproj # Deploy standalone to project"
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --form)
            FORM="$2"
            if [[ "$FORM" != "plugin" && "$FORM" != "standalone" ]]; then
                echo "Error: --form must be 'plugin' or 'standalone'"
                exit 1
            fi
            shift 2
            ;;
        --scope)
            SCOPE="$2"
            if [[ "$SCOPE" != "user" && "$SCOPE" != "project" ]]; then
                echo "Error: --scope must be 'user' or 'project'"
                exit 1
            fi
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        -*)
            echo "Unknown option: $1"
            usage
            ;;
        *)
            PROJECT_PATH="$1"
            shift
            ;;
    esac
done

[[ -z "$FORM" ]] && { echo "Error: --form is required"; usage; }

# Plugin form: just create distribution with instructions
if [[ "$FORM" == "plugin" ]]; then
    echo "Creating plugin distribution..."
    "$SCRIPT_DIR/distribute.sh" --form plugin --output "$PROJECT_ROOT/dist"

    echo ""
    echo "=============================================="
    echo "Plugin distribution created at: $PROJECT_ROOT/dist"
    echo "=============================================="
    echo ""
    echo "To test locally:"
    echo "  claude --plugin-dir $PROJECT_ROOT/dist"
    echo ""
    echo "To distribute via marketplace:"
    echo "  1. Push ./dist contents to agentic-infra/ada repo"
    echo "  2. Add marketplace: claude plugin marketplace add agentic-infra/marketplace"
    echo "  3. Install: claude plugin install ada@marketplace"
    echo ""
    echo "Skills available after loading:"
    echo "  /ada:run     - Launch application with ADA capture"
    echo "  /ada:analyze - Analyze captured session"
    exit 0
fi

# Standalone form: requires scope
[[ -z "$SCOPE" ]] && { echo "Error: --scope is required for standalone form"; usage; }

if [[ "$SCOPE" == "project" ]]; then
    [[ -z "$PROJECT_PATH" ]] && { echo "Error: project_path required when --scope project"; usage; }
    [[ ! -d "$PROJECT_PATH" ]] && { echo "Error: '$PROJECT_PATH' is not a directory"; exit 1; }
fi

# Create distribution
echo "Creating distribution..."
"$SCRIPT_DIR/distribute.sh" --form standalone --output "$PROJECT_ROOT/dist"

# Determine target directory
if [[ "$SCOPE" == "user" ]]; then
    TARGET="$HOME/.claude/commands"
else
    TARGET="$PROJECT_PATH/.claude/commands"
fi

# Deploy
echo ""
echo "Deploying to: $TARGET"
mkdir -p "$TARGET"

cp "$PROJECT_ROOT/dist/"*.md "$TARGET/"

echo ""
echo "Standalone skills deployed successfully!"
echo ""
echo "Files:"
ls -la "$TARGET/"*.md 2>/dev/null | grep -E "run\.md|analyze\.md" || echo "  (no files)"

echo ""
echo "Form: standalone"
echo "Scope: $SCOPE"
[[ -n "$PROJECT_PATH" ]] && echo "Project: $PROJECT_PATH"
echo ""
echo "Usage:"
echo "  /run     - Launch application with ADA capture"
echo "  /analyze - Analyze captured session"
