# ===========================================
# E2E Benchmark Workloads (Xcode-built fixtures)
# ===========================================
# Builds the full (os, toolchain, lang, effective_config) matrix:
#   - 4 languages: C, C++, ObjC, Swift
#   - 5 configs per language:
#       CLI: cli_export_sym, cli_no_export_sym
#       App: app_debug, app_debug_dylib, app_release
#   - Total: 20 benchmark binaries
#
# CLI configs test symbol discovery paths:
#   export_sym:    -Wl,-export_dynamic → gum_module_enumerate_exports (fast)
#   no_export_sym: default linking     → gum_module_enumerate_symbols (slow)
#
# App configs test app bundle tracing scenarios:
#   app_debug:       Debug without ENABLE_DEBUG_DYLIB
#   app_debug_dylib: Debug with ENABLE_DEBUG_DYLIB=YES (debug dylib redirect)
#   app_release:     Release

if(NOT APPLE)
    message(STATUS "E2E benchmark fixtures require macOS (Xcode). Skipping.")
    return()
endif()

# Separate derived data per build to avoid Xcode build service database locks.
set(BENCH_E2E_DERIVED_DATA "${CMAKE_BINARY_DIR}/bench_e2e_derived")

# Fixture paths
set(FIXTURE_BASE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/macos/xcode")
set(FIXTURE_C_CLI "${FIXTURE_BASE}/c/cli")
set(FIXTURE_C_APP "${FIXTURE_BASE}/c/app")
set(FIXTURE_CPP_CLI "${FIXTURE_BASE}/cpp/cli")
set(FIXTURE_CPP_APP "${FIXTURE_BASE}/cpp/app")
set(FIXTURE_OBJC_CLI "${FIXTURE_BASE}/objc/cli")
set(FIXTURE_OBJC_APP "${FIXTURE_BASE}/objc/app")
set(FIXTURE_SWIFT_CLI "${FIXTURE_BASE}/swift/cli")
set(FIXTURE_SWIFT_APP "${FIXTURE_BASE}/swift/app")

# ===========================================================================
# Helper macro: build a CLI fixture with xcodebuild
# ===========================================================================
# Args: TARGET_NAME, PROJECT_DIR, PROJECT_FILE, SCHEME, DERIVED_SUFFIX, OUTPUT_NAME, EXTRA_FLAGS, DEPENDS_TARGET
macro(add_cli_fixture TARGET_NAME PROJECT_DIR PROJECT_FILE SCHEME DERIVED_SUFFIX OUTPUT_NAME EXTRA_FLAGS DEPENDS_TARGET)
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}
        COMMAND xcodebuild
            -project "${PROJECT_DIR}/${PROJECT_FILE}"
            -scheme ${SCHEME}
            -configuration Release
            ${EXTRA_FLAGS}
            -derivedDataPath "${BENCH_E2E_DERIVED_DATA}/${DERIVED_SUFFIX}"
            -quiet
            build
        COMMAND ${CMAKE_COMMAND} -E copy
            "${BENCH_E2E_DERIVED_DATA}/${DERIVED_SUFFIX}/Build/Products/Release/${SCHEME}"
            "${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}"
        COMMENT "Building ${OUTPUT_NAME} with Xcode"
    )
    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME})
    if(NOT "${DEPENDS_TARGET}" STREQUAL "")
        add_dependencies(${TARGET_NAME} ${DEPENDS_TARGET})
    endif()
    # Codesign
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}
        COMMENT "Signing ${OUTPUT_NAME} with entitlements"
        VERBATIM
    )
endmacro()

# ===========================================================================
# Helper macro: build an app bundle fixture with xcodebuild
# ===========================================================================
# Args: TARGET_NAME, PROJECT_DIR, PROJECT_FILE, SCHEME, CONFIGURATION, DERIVED_SUFFIX, OUTPUT_NAME, EXTRA_SETTINGS, DEPENDS_TARGET
macro(add_app_fixture TARGET_NAME PROJECT_DIR PROJECT_FILE SCHEME CONFIGURATION DERIVED_SUFFIX OUTPUT_NAME EXTRA_SETTINGS DEPENDS_TARGET)
    # Stamp file goes OUTSIDE the .app bundle to avoid "unsealed contents" codesign error
    add_custom_command(
        OUTPUT ${CMAKE_BINARY_DIR}/bin/.stamps/${OUTPUT_NAME}.stamp
        COMMAND xcodebuild
            -project "${PROJECT_DIR}/${PROJECT_FILE}"
            -scheme ${SCHEME}
            -configuration ${CONFIGURATION}
            ${EXTRA_SETTINGS}
            -derivedDataPath "${BENCH_E2E_DERIVED_DATA}/${DERIVED_SUFFIX}"
            -quiet
            build
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${BENCH_E2E_DERIVED_DATA}/${DERIVED_SUFFIX}/Build/Products/${CONFIGURATION}/${SCHEME}.app"
            "${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin/.stamps"
        COMMAND ${CMAKE_COMMAND} -E touch "${CMAKE_BINARY_DIR}/bin/.stamps/${OUTPUT_NAME}.stamp"
        COMMENT "Building ${OUTPUT_NAME} with Xcode (${CONFIGURATION})"
    )
    add_custom_target(${TARGET_NAME} ALL
        DEPENDS ${CMAKE_BINARY_DIR}/bin/.stamps/${OUTPUT_NAME}.stamp)
    if(NOT "${DEPENDS_TARGET}" STREQUAL "")
        add_dependencies(${TARGET_NAME} ${DEPENDS_TARGET})
    endif()
    # Codesign with --deep for app bundles (signs embedded .debug.dylib too)
    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND codesign -s "${SIGNING_IDENTITY}" --timestamp=none --deep --entitlements "${CMAKE_SOURCE_DIR}/../utils/ada_entitlements.plist" --force ${CMAKE_BINARY_DIR}/bin/${OUTPUT_NAME}
        COMMENT "Signing ${OUTPUT_NAME} with entitlements (deep)"
        VERBATIM
    )
endmacro()

# ===========================================================================
# C fixtures (5 configs)
# ===========================================================================

# C CLI: export_sym (symbols in export trie via -export_dynamic + visible symbols)
add_cli_fixture(bench_workload_c_cli_export_sym
    "${FIXTURE_C_CLI}" "BenchWorkloadC.xcodeproj" BenchWorkloadC
    c_cli_export_sym bench_workload_c_cli_export_sym
    "OTHER_LDFLAGS=-Wl,-export_dynamic;GCC_SYMBOLS_PRIVATE_EXTERN=NO" "")

# C CLI: no_export_sym (default linking)
add_cli_fixture(bench_workload_c_cli_no_export_sym
    "${FIXTURE_C_CLI}" "BenchWorkloadC.xcodeproj" BenchWorkloadC
    c_cli_no_export_sym bench_workload_c_cli_no_export_sym
    "" bench_workload_c_cli_export_sym)

# C App: debug (no debug dylib)
add_app_fixture(bench_workload_c_app_debug
    "${FIXTURE_C_APP}" "BenchWorkloadCApp.xcodeproj" BenchWorkloadCApp
    Debug c_app_debug "bench_workload_c_app_debug.app"
    "ENABLE_DEBUG_DYLIB=NO" bench_workload_c_cli_no_export_sym)

# C App: debug_dylib (ENABLE_DEBUG_DYLIB=YES)
add_app_fixture(bench_workload_c_app_debug_dylib
    "${FIXTURE_C_APP}" "BenchWorkloadCApp.xcodeproj" BenchWorkloadCApp
    Debug c_app_debug_dylib "bench_workload_c_app_debug_dylib.app"
    "ENABLE_DEBUG_DYLIB=YES" bench_workload_c_app_debug)

# C App: release
add_app_fixture(bench_workload_c_app_release
    "${FIXTURE_C_APP}" "BenchWorkloadCApp.xcodeproj" BenchWorkloadCApp
    Release c_app_release "bench_workload_c_app_release.app"
    "" bench_workload_c_app_debug_dylib)

# ===========================================================================
# C++ fixtures (5 configs)
# ===========================================================================

add_cli_fixture(bench_workload_cpp_cli_export_sym
    "${FIXTURE_CPP_CLI}" "BenchWorkloadCpp.xcodeproj" BenchWorkloadCpp
    cpp_cli_export_sym bench_workload_cpp_cli_export_sym
    "OTHER_LDFLAGS=-Wl,-export_dynamic;GCC_SYMBOLS_PRIVATE_EXTERN=NO" bench_workload_c_app_release)

add_cli_fixture(bench_workload_cpp_cli_no_export_sym
    "${FIXTURE_CPP_CLI}" "BenchWorkloadCpp.xcodeproj" BenchWorkloadCpp
    cpp_cli_no_export_sym bench_workload_cpp_cli_no_export_sym
    "" bench_workload_cpp_cli_export_sym)

add_app_fixture(bench_workload_cpp_app_debug
    "${FIXTURE_CPP_APP}" "BenchWorkloadCppApp.xcodeproj" BenchWorkloadCppApp
    Debug cpp_app_debug "bench_workload_cpp_app_debug.app"
    "ENABLE_DEBUG_DYLIB=NO" bench_workload_cpp_cli_no_export_sym)

add_app_fixture(bench_workload_cpp_app_debug_dylib
    "${FIXTURE_CPP_APP}" "BenchWorkloadCppApp.xcodeproj" BenchWorkloadCppApp
    Debug cpp_app_debug_dylib "bench_workload_cpp_app_debug_dylib.app"
    "ENABLE_DEBUG_DYLIB=YES" bench_workload_cpp_app_debug)

add_app_fixture(bench_workload_cpp_app_release
    "${FIXTURE_CPP_APP}" "BenchWorkloadCppApp.xcodeproj" BenchWorkloadCppApp
    Release cpp_app_release "bench_workload_cpp_app_release.app"
    "" bench_workload_cpp_app_debug_dylib)

# ===========================================================================
# Objective-C fixtures (5 configs)
# ===========================================================================

add_cli_fixture(bench_workload_objc_cli_export_sym
    "${FIXTURE_OBJC_CLI}" "BenchWorkloadObjc.xcodeproj" BenchWorkloadObjc
    objc_cli_export_sym bench_workload_objc_cli_export_sym
    "OTHER_LDFLAGS=-Wl,-export_dynamic;GCC_SYMBOLS_PRIVATE_EXTERN=NO" bench_workload_cpp_app_release)

add_cli_fixture(bench_workload_objc_cli_no_export_sym
    "${FIXTURE_OBJC_CLI}" "BenchWorkloadObjc.xcodeproj" BenchWorkloadObjc
    objc_cli_no_export_sym bench_workload_objc_cli_no_export_sym
    "" bench_workload_objc_cli_export_sym)

add_app_fixture(bench_workload_objc_app_debug
    "${FIXTURE_OBJC_APP}" "BenchWorkloadObjcApp.xcodeproj" BenchWorkloadObjcApp
    Debug objc_app_debug "bench_workload_objc_app_debug.app"
    "ENABLE_DEBUG_DYLIB=NO" bench_workload_objc_cli_no_export_sym)

add_app_fixture(bench_workload_objc_app_debug_dylib
    "${FIXTURE_OBJC_APP}" "BenchWorkloadObjcApp.xcodeproj" BenchWorkloadObjcApp
    Debug objc_app_debug_dylib "bench_workload_objc_app_debug_dylib.app"
    "ENABLE_DEBUG_DYLIB=YES" bench_workload_objc_app_debug)

add_app_fixture(bench_workload_objc_app_release
    "${FIXTURE_OBJC_APP}" "BenchWorkloadObjcApp.xcodeproj" BenchWorkloadObjcApp
    Release objc_app_release "bench_workload_objc_app_release.app"
    "" bench_workload_objc_app_debug_dylib)

# ===========================================================================
# Swift fixtures (5 configs)
# ===========================================================================

add_cli_fixture(bench_workload_swift_cli_export_sym
    "${FIXTURE_SWIFT_CLI}" "BenchWorkloadSwift.xcodeproj" BenchWorkloadSwift
    swift_cli_export_sym bench_workload_swift_cli_export_sym
    "OTHER_LDFLAGS=-Wl,-export_dynamic;GCC_SYMBOLS_PRIVATE_EXTERN=NO" bench_workload_objc_app_release)

add_cli_fixture(bench_workload_swift_cli_no_export_sym
    "${FIXTURE_SWIFT_CLI}" "BenchWorkloadSwift.xcodeproj" BenchWorkloadSwift
    swift_cli_no_export_sym bench_workload_swift_cli_no_export_sym
    "" bench_workload_swift_cli_export_sym)

add_app_fixture(bench_workload_swift_app_debug
    "${FIXTURE_SWIFT_APP}" "BenchWorkloadSwiftApp.xcodeproj" BenchWorkloadSwiftApp
    Debug swift_app_debug "bench_workload_swift_app_debug.app"
    "ENABLE_DEBUG_DYLIB=NO" bench_workload_swift_cli_no_export_sym)

add_app_fixture(bench_workload_swift_app_debug_dylib
    "${FIXTURE_SWIFT_APP}" "BenchWorkloadSwiftApp.xcodeproj" BenchWorkloadSwiftApp
    Debug swift_app_debug_dylib "bench_workload_swift_app_debug_dylib.app"
    "ENABLE_DEBUG_DYLIB=YES" bench_workload_swift_app_debug)

add_app_fixture(bench_workload_swift_app_release
    "${FIXTURE_SWIFT_APP}" "BenchWorkloadSwiftApp.xcodeproj" BenchWorkloadSwiftApp
    Release swift_app_release "bench_workload_swift_app_release.app"
    "" bench_workload_swift_app_debug_dylib)
