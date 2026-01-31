#!/bin/bash
# Build the debug stub test fixture
#
# This script builds the DebugStubApp in Debug configuration with ENABLE_DEBUG_DYLIB=YES,
# which creates a thin stub executable that loads the real app from a .debug.dylib.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "Building DebugStubApp (Debug configuration with ENABLE_DEBUG_DYLIB=YES)..."

xcodebuild \
    -project DebugStubApp.xcodeproj \
    -scheme DebugStubApp \
    -configuration Debug \
    -derivedDataPath build \
    CODE_SIGNING_ALLOWED=NO \
    ENABLE_DEBUG_DYLIB=YES \
    build \
    2>&1 | tail -20

APP_PATH="build/Build/Products/Debug/DebugStubApp.app"

if [ -d "$APP_PATH" ]; then
    echo ""
    echo "Build successful!"
    echo "App location: $SCRIPT_DIR/$APP_PATH"
    echo ""
    echo "Executable contents:"
    ls -la "$APP_PATH/Contents/MacOS/"
    echo ""
    echo "To verify debug dylib stub:"
    echo "  otool -L $APP_PATH/Contents/MacOS/DebugStubApp | grep debug.dylib"
else
    echo "Build failed - app not found at $APP_PATH"
    exit 1
fi
