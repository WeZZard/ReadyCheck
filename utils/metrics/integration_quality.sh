#!/bin/bash
# Quality Gate Enforcement Script with Cargo Orchestration
# All builds and coverage collection go through Cargo

set -uo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
MODE="${1:-full}"  # full, incremental, score-only
VERBOSE="${2:-false}"

# Integration score tracking
INTEGRATION_SCORE=100
CRITICAL_FAILURES=()

CHECK_RESULT_PASSED=0
CHECK_RESULT_BLOCKED=1
CHECK_RESULT_DOCUMENT_ONLY=2

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[✓]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[⚠]${NC} $1"
}

log_error() {
    echo -e "${RED}[✗]${NC} $1"
}

deduct_points() {
    local points=$1
    local reason=$2
    INTEGRATION_SCORE=$((INTEGRATION_SCORE - points))
    CRITICAL_FAILURES+=("$reason (-$points points)")
    log_error "$reason"
}

# Build all components through Cargo
build_all() {
    log_info "Building all components through Cargo..."
    
    local BUILD_FLAGS=""
    
    # Enable coverage instrumentation if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        BUILD_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        export RUSTFLAGS="-C instrument-coverage"
        export LLVM_PROFILE_FILE="${REPO_ROOT}/target/coverage/prof-%p-%m.profraw"
        log_info "Coverage instrumentation enabled"
    fi
    
    # Build everything through Cargo
    if ! cargo build --all --release $BUILD_FLAGS; then
        deduct_points 100 "Build failed"
        return 1
    fi
    
    log_success "Build completed successfully"
    
    # Mandatory code signing on macOS
    if [[ "$OSTYPE" == "darwin"* ]]; then
        sign_test_binaries
    fi
}

is_ci() {
  # Generic flag plus common vendor-specific toggles
  [ -n "$CI" ] ||
  [ -n "$GITHUB_ACTIONS" ] ||
  [ -n "$GITLAB_CI" ] ||
  [ -n "$BUILDKITE" ] ||
  [ -n "$CIRCLECI" ] ||
  [ -n "$TEAMCITY_VERSION" ] ||
  [ -n "$JENKINS_URL" ] ||
  [ -n "$BITBUCKET_BUILD_NUMBER" ] ||
  [ -n "$TRAVIS" ] ||
  [ -n "$APPVEYOR" ]
}

is_ssh() {
  # Fast-path: env vars set by ssh/mosh
  if [ -n "$SSH_CONNECTION" ] || [ -n "$SSH_TTY" ] || [ -n "$SSH_CLIENT" ] || [ -n "$MOSH_CONNECTION" ]; then
    return 0
  fi

  # Sudo can scrub SSH_*; fall back to parent process name
  # (portable enough for macOS/Linux; ignore errors if ps differs)
  p=$(ps -o comm= -p "$PPID" 2>/dev/null || true)
  echo "$p" | grep -Eq '(^|/)(sshd|mosh-server)$'
}

# Sign test binaries (MANDATORY on macOS)
sign_test_binaries() {
    log_info "Signing test binaries (MANDATORY on macOS)..."
    
    # Check for Apple Developer certificate in CI or SSH session
    if  ( is_ci || is_ssh ) && [ -z "${APPLE_DEVELOPER_ID:-}" ]; then
        log_error "APPLE_DEVELOPER_ID environment variable not set"
        log_error ""
        log_error "You MUST have an Apple Developer certificate to develop ADA on macOS."
        log_error "This is mandatory for remote development and testing."
        log_error ""
        log_error "To fix this:"
        log_error "1. Get Apple Developer membership (\$99/year): https://developer.apple.com/programs/"
        log_error "2. Create Developer ID certificate in Xcode"
        log_error "3. Export APPLE_DEVELOPER_ID='Developer ID Application: Your Name (TEAMID)'"
        log_error ""
        log_error "See: docs/GETTING_STARTED.md#macos-apple-developer-certificate-mandatory"
        deduct_points 100 "No Apple Developer certificate configured"
        CRITICAL_FAILURES+=("Apple Developer certificate required but not configured")
        return 1
    fi
    
    # Sign all test binaries
    local sign_count=0
    local sign_failures=0
    
    # Find all test binaries
    while IFS= read -r binary; do
        ((sign_count++))
        log_info "Signing: $binary"
        # Use signing script and show output for debugging
        if ! "${REPO_ROOT}/utils/sign_binary.sh" "$binary"; then
            log_error "Failed to sign: $binary"
            ((sign_failures++))
        fi
    done < <(find "${REPO_ROOT}/target" -name "test_*" -type f -perm +111 2>/dev/null)
    
    if [[ $sign_count -eq 0 ]]; then
        log_warning "No test binaries found to sign"
        return 0
    fi
    
    if [[ $sign_failures -gt 0 ]]; then
        log_error "Failed to sign $sign_failures out of $sign_count binaries"
        deduct_points 100 "Binary signing failed"
        CRITICAL_FAILURES+=("Test binary signing failed")
        return 1
    fi
    
    log_success "Successfully signed $sign_count test binaries"
    return 0
}

# Run all tests through Cargo
run_tests() {
    log_info "Running all tests through Cargo..."
    
    local TEST_FLAGS=""
    
    # Enable coverage if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        TEST_FLAGS="--features tracer_backend/coverage,query_engine/coverage"
        # Ensure coverage directory exists
        mkdir -p "${REPO_ROOT}/target/coverage"
    fi
    
    # Run all tests
    if ! cargo test --all $TEST_FLAGS; then
        deduct_points 100 "Tests failed"
        return 1
    fi
    
    log_success "All tests passed"
}

# Collect coverage data using Cargo-orchestrated helper
collect_coverage() {
    if [[ "$MODE" == "score-only" ]]; then
        log_info "Skipping coverage collection in score-only mode"
        return 0
    fi
    
    log_info "Collecting coverage data through Cargo..."
    
    # Run the coverage helper to collect all coverage data
    if ! cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- full; then
        log_warning "Coverage collection failed"
        return 1
    fi
    
    # Check if coverage meets requirements using the merged LCOV
    if [[ -f "${REPO_ROOT}/target/coverage_report/merged.lcov" ]]; then
        local lines_hit=$(grep -c "DA:[0-9]*,[1-9]" "${REPO_ROOT}/target/coverage_report/merged.lcov" 2>/dev/null || echo "0")
        local lines_total=$(grep -c "DA:" "${REPO_ROOT}/target/coverage_report/merged.lcov" 2>/dev/null || echo "0")
        
        if [[ $lines_total -gt 0 ]]; then
            local coverage=$(awk "BEGIN {printf \"%.2f\", ($lines_hit / $lines_total) * 100}")
            log_success "Total coverage: ${coverage}%"
            
            # Check incremental coverage for changed files
            if [[ "$MODE" == "incremental" ]]; then
                check_incremental_coverage
            fi
        fi
    fi
}

# Check if only documentation files were changed
is_documentation_only() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        # Pre-commit: check staged files
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        # Post-commit: check last commit
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1  # No git context, assume not docs-only
    fi
    
    # If no files changed, not documentation-only
    if [[ -z "$changed_files" ]]; then
        return 1
    fi
    
    # Check if ALL changed files are documentation or non-code files
    local non_doc_files=$(echo "$changed_files" | grep -v -E '\.(md|txt|rst|adoc|org)$' | \
        grep -v -E '^(docs/|documentation/|README|LICENSE|CHANGELOG|AUTHORS|CONTRIBUTORS|\.gitignore|\.gitattributes)' || true)
    
    if [[ -z "$non_doc_files" ]]; then
        return 0  # Only documentation files changed
    else
        return 1  # Has non-documentation changes
    fi
}

# Check if any source files were changed
has_source_changes() {
    local changed_files
    if git diff --cached --name-only &>/dev/null; then
        # Pre-commit: check staged files
        changed_files=$(git diff --cached --name-only)
    elif git diff HEAD~1 --name-only &>/dev/null; then
        # Post-commit: check last commit
        changed_files=$(git diff HEAD~1 --name-only)
    else
        return 1  # No git context, assume changes
    fi
    
    # Filter for actual source code files (not tests, not config, not docs)
    # First get all code files
    local code_files=$(echo "$changed_files" | grep -E '\.(rs|c|cpp|cc|cxx|h|hpp|hxx|py)$' || true)
    
    # Exclude test files, build scripts, and utility scripts
    local source_files=$(echo "$code_files" | \
        grep -v -E '(^tests?/|/tests?/|_test\.|\.test\.|test_.*\.rs$|.*_test\.py$|/fixtures?/|/bench/|/benches/|build\.rs$)' || true)
    
    # Exclude utility and helper directories (these are tools, not product code)
    source_files=$(echo "$source_files" | \
        grep -v -E '(^utils/|^tools/|^scripts/|^\.github/)' || true)
    
    # Keep only files in component directories (where actual product code lives)
    source_files=$(echo "$source_files" | \
        grep -E '(^tracer/|^tracer_backend/|^query_engine/|^mcp_server/|^src/|^include/|^lib/)' || true)
    
    if [[ -z "$source_files" ]]; then
        return 1  # No source changes
    else
        echo "$source_files"  # Output the list for use by caller
        return 0  # Has source changes
    fi
}

# Check incremental coverage for changed files
check_incremental_coverage() {
    log_info "Checking incremental coverage for changed lines..."
    
    # Get changed source files (excluding tests)
    local changed_files=$(has_source_changes || echo "")
    
    if [[ -z "$changed_files" ]]; then
        log_info "No source files changed (only config/docs/tests)"
        return 0
    fi
    
    log_info "Source files with changes:"
    echo "$changed_files" | sed 's/^/  - /'
    
    # Check if merged LCOV exists
    local merged_lcov="${REPO_ROOT}/target/coverage_report/merged.lcov"
    if [[ ! -f "$merged_lcov" ]]; then
        log_error "No coverage data found. Run tests with coverage first."
        deduct_points 100 "Coverage data missing"
        return 1
    fi
    
    # Determine comparison branch
    local compare_branch="HEAD~1"
    if git rev-parse --verify origin/main &>/dev/null; then
        compare_branch="origin/main"
    fi
    
    log_info "Comparing against: $compare_branch"
    
    # Run diff-cover with 100% requirement for changed lines
    if diff-cover "$merged_lcov" \
                  --fail-under=100 \
                  --compare-branch="$compare_branch" 2>&1; then
        log_success "All changed lines have 100% coverage"
        return 0
    else
        log_error "Some changed lines lack coverage"
        deduct_points 100 "Changed lines must have 100% test coverage"
        
        # Generate HTML report for detailed view
        local report_dir="${REPO_ROOT}/target/coverage_report"
        diff-cover "$merged_lcov" \
                   --compare-branch="$compare_branch" \
                   --html-report="${report_dir}/diff-coverage.html" &>/dev/null || true
        
        log_warning "View detailed report: file://${report_dir}/diff-coverage.html"
        return 1
    fi
}

# Check for incomplete implementations
check_incomplete() {
    log_info "Checking for incomplete implementations..."
    
    local incomplete_count=0
    
    # Check for TODO/FIXME/XXX comments
    incomplete_count=$(grep -r "TODO\|FIXME\|XXX" --include="*.rs" --include="*.c" --include="*.cpp" --include="*.h" "${REPO_ROOT}" 2>/dev/null | wc -l || echo 0)
    
    if [[ $incomplete_count -gt 0 ]]; then
        log_warning "Found $incomplete_count incomplete implementations (TODO/FIXME/XXX)"
    else
        log_success "No incomplete implementations found"
    fi
}

# Open coverage report in browser
open_coverage_report() {
    local report_path="${REPO_ROOT}/target/coverage_report/index.html"
    
    # Check if report exists
    if [[ ! -f "$report_path" ]]; then
        # Try diff coverage report
        report_path="${REPO_ROOT}/target/coverage_report/diff-coverage.html"
        if [[ ! -f "$report_path" ]]; then
            log_warning "No coverage report found. Run coverage collection first."
            return 1
        fi
    fi
    
    log_info "Opening coverage report in browser..."
    
    # Platform-specific browser opening
    case "$OSTYPE" in
        darwin*)
            open "$report_path"
            ;;
        linux*)
            if command -v xdg-open &>/dev/null; then
                xdg-open "$report_path"
            else
                log_info "Report available at: file://$report_path"
            fi
            ;;
        msys*|cygwin*)
            start "$report_path"
            ;;
        *)
            log_info "Report available at: file://$report_path"
            ;;
    esac
}

# Generate final report
generate_report() {
    echo ""
    echo "========================================="
    echo "       INTEGRATION QUALITY REPORT        "
    echo "========================================="
    echo ""
    
    if [[ ${#CRITICAL_FAILURES[@]} -gt 0 ]]; then
        echo -e "${RED}CRITICAL FAILURES:${NC}"
        for failure in "${CRITICAL_FAILURES[@]}"; do
            echo "  • $failure"
        done
        echo ""
    fi
    
    # Color code the score
    local score_color=$GREEN
    if [[ $INTEGRATION_SCORE -lt 100 ]]; then
        score_color=$RED
    fi
    
    echo -e "Integration Score: ${score_color}${INTEGRATION_SCORE}/100${NC}"
    
    if [[ $INTEGRATION_SCORE -eq 100 ]]; then
        echo -e "${GREEN}✓ Quality Gate PASSED${NC}"
        return $CHECK_RESULT_PASSED
    else
        echo -e "${RED}✗ Quality Gate FAILED${NC}"
        return $CHECK_RESULT_BLOCKED
    fi
}

check_quality() {
    log_info "Running quality gate in $MODE mode"
    
    # For incremental mode, check if there are any source changes first
    if [[ "$MODE" == "incremental" ]]; then
        # First check if ONLY documentation files were changed
        if is_documentation_only; then
            log_success "Only documentation files detected"
            log_info "Skipping build, tests, and coverage for documentation-only changes"
            return $CHECK_RESULT_DOCUMENT_ONLY
        fi
        
        # Check if there are source code changes
        local source_changes=$(has_source_changes || echo "")
        if [[ -z "$source_changes" ]]; then
            log_success "No source code changes detected"
            log_info "Only configuration, documentation, or test files were modified"
            log_success "Skipping coverage requirements for non-source changes"
            
            # Still run basic checks for config/test changes
            build_all
            run_tests
            generate_report
            return $?
        else
            log_info "Source code changes detected - full quality gate required"
        fi
    fi
    
    # Clean coverage data if in full mode
    if [[ "$MODE" == "full" ]]; then
        log_info "Cleaning previous coverage data..."
        cargo run --manifest-path "${REPO_ROOT}/utils/coverage_helper/Cargo.toml" -- clean
    fi
    
    # Build everything
    build_all
    
    # Run tests
    run_tests
    
    # Check incomplete implementations
    check_incomplete
    
    # Collect coverage if not in score-only mode
    if [[ "$MODE" != "score-only" ]]; then
        collect_coverage
    fi
    
    # Generate final report
    generate_report
    return $?
}

# Main execution
main() {
    check_quality
    CHECK_RESULT=$?
    case $CHECK_RESULT in
        $CHECK_RESULT_PASSED)
        # Success
        echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║                   QUALITY GATE PASSED ✓                    ║${NC}"
        echo -e "${GREEN}╠════════════════════════════════════════════════════════════╣${NC}"
        echo -e "${GREEN}║  All critical quality checks passed:                       ║${NC}"
        echo -e "${GREEN}║  • Build successful                                        ║${NC}"
        echo -e "${GREEN}║  • All tests passing                                       ║${NC}"
        echo -e "${GREEN}║  • Coverage requirements met                               ║${NC}"
        echo -e "${GREEN}║  • No incomplete implementations                           ║${NC}"
        echo -e "${GREEN}║  • No binary files or secrets detected                     ║${NC}"
        echo -e "${GREEN}║                                                            ║${NC}"
        echo -e "${GREEN}║  Proceeding with commit...                                 ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
        return 0;
        ;;
        $CHECK_RESULT_BLOCKED)
        echo -e   "${RED}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e   "${RED}║            COMMIT BLOCKED: QUALITY GATE FAILED             ║${NC}"
        echo -e   "${RED}╠════════════════════════════════════════════════════════════╣${NC}"
        echo -e   "${RED}║  Critical quality gates must pass before committing:       ║${NC}"
        echo -e   "${RED}║                                                            ║${NC}"
        echo -e   "${RED}║  • Build must succeed (100% required)                      ║${NC}"
        echo -e   "${RED}║  • All tests must pass (100% required)                     ║${NC}"
        echo -e   "${RED}║  • Changed code must have 100% test coverage               ║${NC}"
        echo -e   "${RED}║  • No incomplete implementations (todo!/assert(0)/etc)     ║${NC}"
        echo -e   "${RED}║                                                            ║${NC}"
        echo -e   "${RED}║  Per CLAUDE.md requirements:                               ║${NC}"
        echo -e   "${RED}║  - NO ignoring tests                                       ║${NC}"
        echo -e   "${RED}║  - NO reducing requirements                                ║${NC}"
        echo -e   "${RED}║  - NO temporary quality compromises                        ║${NC}"
        echo -e   "${RED}║  - NO git commit --no-verify                               ║${NC}"
        echo -e   "${RED}║                                                            ║${NC}"
        echo -e   "${RED}║  Fix the issues and try again.                             ║${NC}"
        echo -e   "${RED}╚════════════════════════════════════════════════════════════╝${NC}"
        return 1;
        ;;
        $CHECK_RESULT_DOCUMENT_ONLY)
        echo -e "${GREEN}╔════════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║            DOCUMENTATION-ONLY CHANGES DETECTED             ║${NC}"
        echo -e "${GREEN}╠════════════════════════════════════════════════════════════╣${NC}"
        echo -e "${GREEN}║  Only markdown/text files were modified.                   ║${NC}"
        echo -e "${GREEN}║  Skipping unnecessary quality checks:                      ║${NC}"
        echo -e "${GREEN}║  • Build - not needed for documentation                    ║${NC}"
        echo -e "${GREEN}║  • Tests - not affected by documentation                   ║${NC}"
        echo -e "${GREEN}║  • Coverage - no code changes to measure                   ║${NC}"
        echo -e "${GREEN}╚════════════════════════════════════════════════════════════╝${NC}"
        return 0;
        ;;
        *)
        echo "Invalid check result: $CHECK_RESULT. Please check the logs for more details."
        return 1;
        ;;
    esac
}
# Run main function
main
MAIN_RESULT=$?

# Handle --show-report flag
if [[ "${2:-}" == "--show-report" ]] || [[ "${3:-}" == "--show-report" ]]; then
    open_coverage_report
fi

exit $MAIN_RESULT
