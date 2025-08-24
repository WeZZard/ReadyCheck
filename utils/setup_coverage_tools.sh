#!/bin/bash
# Setup script for coverage tools - used by both developers and CI
# This ensures all required tools are available for Option A coverage architecture

set -e

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "======================================"
echo "    Coverage Tools Setup Check"
echo "======================================"
echo ""
echo "Checking for required coverage tools per Option A architecture..."
echo ""

# Track if any tool is missing
MISSING=0

# Function to check for a tool
check_tool() {
    local tool=$1
    local install_cmd=$2
    local purpose=$3
    
    if command -v "$tool" &> /dev/null; then
        echo -e "${GREEN}✅${NC} $tool found - $purpose"
        return 0
    else
        echo -e "${RED}❌${NC} $tool not found - $purpose"
        echo -e "   ${YELLOW}Install:${NC} $install_cmd"
        MISSING=1
        return 1
    fi
}

# Function to check Rust component in cargo
check_cargo_tool() {
    local tool=$1
    local install_cmd=$2
    local purpose=$3
    
    if cargo --list | grep -q "^    $tool$"; then
        echo -e "${GREEN}✅${NC} cargo-$tool found - $purpose"
        return 0
    else
        echo -e "${RED}❌${NC} cargo-$tool not found - $purpose"
        echo -e "   ${YELLOW}Install:${NC} $install_cmd"
        MISSING=1
        return 1
    fi
}

# Check Python module
check_python_module() {
    local module=$1
    local install_cmd=$2
    local purpose=$3
    
    if python3 -c "import $module" 2>/dev/null; then
        echo -e "${GREEN}✅${NC} Python module '$module' found - $purpose"
        return 0
    else
        echo -e "${RED}❌${NC} Python module '$module' not found - $purpose"
        echo -e "   ${YELLOW}Install:${NC} $install_cmd"
        MISSING=1
        return 1
    fi
}

echo "Rust Coverage Tools:"
echo "--------------------"
check_cargo_tool "llvm-cov" "cargo install cargo-llvm-cov" "Rust coverage collection"

# Check for LLVM tools (usually comes with Rust)
if rustup component list --installed | grep -q "llvm-tools"; then
    echo -e "${GREEN}✅${NC} llvm-tools-preview installed"
else
    echo -e "${YELLOW}⚠️${NC} llvm-tools-preview not installed"
    echo -e "   ${YELLOW}Install:${NC} rustup component add llvm-tools-preview"
    MISSING=1
fi

echo ""
echo "C/C++ Coverage Tools:"
echo "---------------------"
check_tool "clang" "brew install llvm (macOS) / apt install clang (Linux)" "C/C++ compilation with coverage"
check_tool "llvm-cov" "brew install llvm (macOS) / apt install llvm (Linux)" "LLVM coverage export"
check_tool "llvm-profdata" "Comes with llvm" "LLVM profile data merging"

echo ""
echo "Python Coverage Tools:"
echo "----------------------"
check_python_module "pytest" "pip install pytest" "Python testing framework"
check_python_module "pytest_cov" "pip install pytest-cov" "pytest coverage plugin"
check_python_module "coverage" "pip install coverage" "Python coverage collection"

# Check for coverage-lcov converter
if python3 -c "import coverage_lcov" 2>/dev/null || command -v coverage-lcov &>/dev/null; then
    echo -e "${GREEN}✅${NC} coverage-lcov found - Python LCOV converter"
else
    echo -e "${RED}❌${NC} coverage-lcov not found - Python LCOV converter"
    echo -e "   ${YELLOW}Install:${NC} pip install coverage-lcov"
    MISSING=1
fi

echo ""
echo "LCOV Merging & Reporting Tools:"
echo "--------------------------------"
check_tool "lcov" "brew install lcov (macOS) / apt install lcov (Linux)" "LCOV file merging"
check_tool "genhtml" "Comes with lcov package" "HTML report generation"

echo ""
echo "Changed-Line Coverage Tools:"
echo "-----------------------------"
# Check for diff-cover
if command -v diff-cover &>/dev/null; then
    echo -e "${GREEN}✅${NC} diff-cover found - Changed-line coverage enforcement"
else
    echo -e "${RED}❌${NC} diff-cover not found - Changed-line coverage enforcement"
    echo -e "   ${YELLOW}Install:${NC} pip install diff-cover"
    MISSING=1
fi

echo ""
echo "======================================"

if [ $MISSING -eq 0 ]; then
    echo -e "${GREEN}✅ All coverage tools are installed!${NC}"
    echo ""
    echo "You can now run coverage collection with:"
    echo "  cargo run --manifest-path utils/coverage_helper/Cargo.toml -- full"
    echo ""
    echo "Or check changed-line coverage with:"
    echo "  cargo run --manifest-path utils/coverage_helper/Cargo.toml -- incremental"
    exit 0
else
    echo -e "${RED}⚠️  Some tools are missing.${NC}"
    echo ""
    echo "Please install the missing tools listed above."
    echo ""
    echo "Quick install for macOS:"
    echo "  brew install llvm lcov"
    echo "  pip install pytest pytest-cov coverage coverage-lcov diff-cover"
    echo "  cargo install cargo-llvm-cov"
    echo "  rustup component add llvm-tools-preview"
    echo ""
    echo "Quick install for Ubuntu/Debian:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install -y clang llvm lcov"
    echo "  pip install pytest pytest-cov coverage coverage-lcov diff-cover"
    echo "  cargo install cargo-llvm-cov"
    echo "  rustup component add llvm-tools-preview"
    exit 1
fi