#!/bin/bash
# Initialize media tool dependencies for ADA project
#
# Downloads/builds:
# - FFmpeg: Pre-built static binaries for macOS arm64
# - whisper.cpp: Built from source with Metal support
#
# Following the same pattern as init_third_parties.sh

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Configuration
WHISPER_CPP_TAG="v1.7.3"
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
THIRD_PARTIES_DIR="$PROJECT_ROOT/third_parties"

# Platform detection
detect_platform() {
    local os=$(uname -s | tr '[:upper:]' '[:lower:]')
    local arch=$(uname -m)

    case "$os" in
        darwin)
            os="macos"
            ;;
        linux)
            os="linux"
            ;;
        *)
            echo -e "${RED}Error: Unsupported OS: $os${NC}"
            exit 1
            ;;
    esac

    case "$arch" in
        x86_64)
            arch="x86_64"
            ;;
        arm64|aarch64)
            arch="arm64"
            ;;
        *)
            echo -e "${RED}Error: Unsupported architecture: $arch${NC}"
            exit 1
            ;;
    esac

    echo "${os}-${arch}"
}

# Download a file if it doesn't already exist
download_if_needed() {
    local url="$1"
    local output_file="$2"

    if [ -f "$output_file" ]; then
        echo -e "${YELLOW}File already exists: $output_file${NC}"
        return 0
    fi

    echo -e "${BLUE}Downloading: $url${NC}"
    if command -v curl &> /dev/null; then
        curl -L --progress-bar -o "$output_file" "$url" || {
            echo -e "${RED}Failed to download $url${NC}"
            rm -f "$output_file"
            return 1
        }
    elif command -v wget &> /dev/null; then
        wget -O "$output_file" "$url" || {
            echo -e "${RED}Failed to download $url${NC}"
            rm -f "$output_file"
            return 1
        }
    else
        echo -e "${RED}Error: Neither curl nor wget is available${NC}"
        return 1
    fi

    echo -e "${GREEN}Downloaded: $output_file${NC}"
}

# ============================================================
# FFmpeg: Download pre-built static binaries
# ============================================================
setup_ffmpeg() {
    local platform="$1"
    local ffmpeg_dir="$THIRD_PARTIES_DIR/ffmpeg-bin"

    echo -e "${GREEN}=== Setting up FFmpeg ===${NC}"

    if [[ -f "$ffmpeg_dir/ffmpeg" && -f "$ffmpeg_dir/ffprobe" ]]; then
        echo -e "${YELLOW}FFmpeg binaries already exist at $ffmpeg_dir${NC}"
        return 0
    fi

    mkdir -p "$ffmpeg_dir"

    # Download from ffmpeg.martin-riedl.de (macOS arm64 + x86_64 static builds)
    local base_url="https://ffmpeg.martin-riedl.de/redirect/latest"

    case "$platform" in
        macos-arm64)
            local ffmpeg_url="${base_url}/macos/arm64/release/ffmpeg.zip"
            local ffprobe_url="${base_url}/macos/arm64/release/ffprobe.zip"
            ;;
        macos-x86_64)
            local ffmpeg_url="${base_url}/macos/amd64/release/ffmpeg.zip"
            local ffprobe_url="${base_url}/macos/amd64/release/ffprobe.zip"
            ;;
        linux-x86_64)
            local ffmpeg_url="${base_url}/linux/amd64/release/ffmpeg.tar.xz"
            local ffprobe_url="${base_url}/linux/amd64/release/ffprobe.tar.xz"
            ;;
        linux-arm64)
            local ffmpeg_url="${base_url}/linux/arm64/release/ffmpeg.tar.xz"
            local ffprobe_url="${base_url}/linux/arm64/release/ffprobe.tar.xz"
            ;;
        *)
            echo -e "${RED}No pre-built FFmpeg available for $platform${NC}"
            return 1
            ;;
    esac

    local ffmpeg_archive="$THIRD_PARTIES_DIR/ffmpeg-download.${ffmpeg_url##*.}"
    local ffprobe_archive="$THIRD_PARTIES_DIR/ffprobe-download.${ffprobe_url##*.}"

    # Download and extract ffmpeg
    download_if_needed "$ffmpeg_url" "$ffmpeg_archive" || return 1

    if [[ "$ffmpeg_archive" == *.zip ]]; then
        unzip -o "$ffmpeg_archive" -d "$ffmpeg_dir" || {
            echo -e "${RED}Failed to extract ffmpeg${NC}"
            return 1
        }
    else
        tar xf "$ffmpeg_archive" -C "$ffmpeg_dir" || {
            echo -e "${RED}Failed to extract ffmpeg${NC}"
            return 1
        }
    fi

    # Download and extract ffprobe
    download_if_needed "$ffprobe_url" "$ffprobe_archive" || true

    if [[ -f "$ffprobe_archive" ]]; then
        if [[ "$ffprobe_archive" == *.zip ]]; then
            unzip -o "$ffprobe_archive" -d "$ffmpeg_dir" 2>/dev/null || {
                echo -e "${YELLOW}Warning: Could not extract ffprobe${NC}"
            }
        else
            tar xf "$ffprobe_archive" -C "$ffmpeg_dir" 2>/dev/null || {
                echo -e "${YELLOW}Warning: Could not extract ffprobe${NC}"
            }
        fi
    fi

    # Make executable
    chmod +x "$ffmpeg_dir/ffmpeg" 2>/dev/null || true
    chmod +x "$ffmpeg_dir/ffprobe" 2>/dev/null || true

    # Verify
    if [[ -f "$ffmpeg_dir/ffmpeg" ]]; then
        echo -e "${GREEN}FFmpeg installed at: $ffmpeg_dir/ffmpeg${NC}"
        "$ffmpeg_dir/ffmpeg" -version 2>/dev/null | head -1 || true
    else
        echo -e "${RED}FFmpeg binary not found after setup${NC}"
        return 1
    fi

    if [[ -f "$ffmpeg_dir/ffprobe" ]]; then
        echo -e "${GREEN}FFprobe installed at: $ffmpeg_dir/ffprobe${NC}"
    else
        echo -e "${YELLOW}Warning: FFprobe not available. Some features may be limited.${NC}"
    fi
}

# ============================================================
# whisper.cpp: Build from source with Metal support
# ============================================================
setup_whisper_cpp() {
    local platform="$1"
    local whisper_bin_dir="$THIRD_PARTIES_DIR/whisper-bin"
    local whisper_src_dir="$THIRD_PARTIES_DIR/whisper.cpp"

    echo -e "${GREEN}=== Setting up whisper.cpp ===${NC}"

    if [[ -f "$whisper_bin_dir/whisper-cli" ]]; then
        echo -e "${YELLOW}whisper-cli already exists at $whisper_bin_dir${NC}"
        return 0
    fi

    mkdir -p "$whisper_bin_dir"

    # Clone whisper.cpp at pinned tag
    if [[ ! -d "$whisper_src_dir" ]]; then
        echo -e "${BLUE}Cloning whisper.cpp at tag $WHISPER_CPP_TAG...${NC}"
        git clone --depth 1 --branch "$WHISPER_CPP_TAG" \
            https://github.com/ggerganov/whisper.cpp.git \
            "$whisper_src_dir" || {
            echo -e "${RED}Failed to clone whisper.cpp${NC}"
            return 1
        }
    else
        echo -e "${YELLOW}whisper.cpp source already cloned${NC}"
    fi

    # Build with CMake
    local build_dir="$whisper_src_dir/build"
    mkdir -p "$build_dir"

    echo -e "${BLUE}Building whisper.cpp...${NC}"

    local cmake_flags="-DCMAKE_BUILD_TYPE=Release"

    # Enable Metal on macOS
    if [[ "$platform" == macos-* ]]; then
        cmake_flags="$cmake_flags -DWHISPER_METAL=ON"
        echo -e "${BLUE}Metal acceleration enabled${NC}"
    fi

    cmake -S "$whisper_src_dir" -B "$build_dir" $cmake_flags || {
        echo -e "${RED}CMake configure failed${NC}"
        return 1
    }

    cmake --build "$build_dir" --config Release -j$(nproc 2>/dev/null || sysctl -n hw.logicalcpu) || {
        echo -e "${RED}Build failed${NC}"
        return 1
    }

    # Find and copy the whisper-cli binary
    local cli_binary=""
    for candidate in "$build_dir/bin/whisper-cli" "$build_dir/whisper-cli" "$build_dir/bin/main"; do
        if [[ -f "$candidate" ]]; then
            cli_binary="$candidate"
            break
        fi
    done

    if [[ -z "$cli_binary" ]]; then
        echo -e "${RED}Could not find whisper-cli binary after build${NC}"
        echo "Build directory contents:"
        find "$build_dir" -name "whisper*" -o -name "main" 2>/dev/null | head -20
        return 1
    fi

    cp "$cli_binary" "$whisper_bin_dir/whisper-cli"
    chmod +x "$whisper_bin_dir/whisper-cli"

    # Copy Metal library if present
    if [[ -f "$build_dir/bin/ggml-metal.metal" ]]; then
        cp "$build_dir/bin/ggml-metal.metal" "$whisper_bin_dir/"
    fi
    if [[ -f "$build_dir/ggml-metal.metal" ]]; then
        cp "$build_dir/ggml-metal.metal" "$whisper_bin_dir/"
    fi

    echo -e "${GREEN}whisper-cli installed at: $whisper_bin_dir/whisper-cli${NC}"
    "$whisper_bin_dir/whisper-cli" --help 2>&1 | head -3 || true
}

# ============================================================
# Download whisper model to project-local cache
# ============================================================
download_whisper_model() {
    local model_name="${1:-tiny}"
    local models_dir="$THIRD_PARTIES_DIR/whisper-models"
    local model_file="ggml-${model_name}.bin"
    local model_url="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${model_file}"

    echo -e "${GREEN}=== Downloading whisper model: $model_name ===${NC}"

    if [[ -f "$models_dir/$model_file" ]]; then
        echo -e "${YELLOW}Model already exists: $models_dir/$model_file${NC}"
        return 0
    fi

    mkdir -p "$models_dir"
    download_if_needed "$model_url" "$models_dir/$model_file" || return 1

    echo -e "${GREEN}Model downloaded: $models_dir/$model_file${NC}"
}

# ============================================================
# Main
# ============================================================
main() {
    echo -e "${GREEN}=== ADA Media Tools Initialization ===${NC}"
    echo -e "${BLUE}Project root: $PROJECT_ROOT${NC}"

    PLATFORM=$(detect_platform)
    echo -e "${BLUE}Detected platform: $PLATFORM${NC}"

    mkdir -p "$THIRD_PARTIES_DIR"

    # Setup FFmpeg
    setup_ffmpeg "$PLATFORM" || {
        echo -e "${YELLOW}FFmpeg setup had issues. You can install manually: brew install ffmpeg${NC}"
    }

    echo ""

    # Setup whisper.cpp
    setup_whisper_cpp "$PLATFORM" || {
        echo -e "${YELLOW}whisper.cpp setup had issues. Check build prerequisites (cmake, Xcode CLI tools).${NC}"
    }

    echo ""

    # Download default whisper model (required for bundling)
    download_whisper_model "${1:-tiny}" || {
        echo -e "${YELLOW}Model download had issues. It will be downloaded on first use.${NC}"
    }

    echo ""

    echo -e "${GREEN}=== Summary ===${NC}"
    echo ""
    echo "FFmpeg:"
    if [[ -f "$THIRD_PARTIES_DIR/ffmpeg-bin/ffmpeg" ]]; then
        echo -e "  ${GREEN}✓${NC} ffmpeg:    $THIRD_PARTIES_DIR/ffmpeg-bin/ffmpeg"
    else
        echo -e "  ${RED}✗${NC} ffmpeg:    not found"
    fi
    if [[ -f "$THIRD_PARTIES_DIR/ffmpeg-bin/ffprobe" ]]; then
        echo -e "  ${GREEN}✓${NC} ffprobe:   $THIRD_PARTIES_DIR/ffmpeg-bin/ffprobe"
    else
        echo -e "  ${YELLOW}⚠${NC} ffprobe:   not found"
    fi
    echo ""
    echo "whisper.cpp:"
    if [[ -f "$THIRD_PARTIES_DIR/whisper-bin/whisper-cli" ]]; then
        echo -e "  ${GREEN}✓${NC} whisper-cli: $THIRD_PARTIES_DIR/whisper-bin/whisper-cli"
    else
        echo -e "  ${RED}✗${NC} whisper-cli: not found"
    fi
    echo ""
    echo "Whisper model:"
    if [[ -f "$THIRD_PARTIES_DIR/whisper-models/ggml-tiny.bin" ]]; then
        echo -e "  ${GREEN}✓${NC} ggml-tiny.bin: $THIRD_PARTIES_DIR/whisper-models/ggml-tiny.bin"
    else
        echo -e "  ${RED}✗${NC} ggml-tiny.bin: not found"
    fi
}

main "$@"
