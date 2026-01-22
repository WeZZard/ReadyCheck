// test_swift_simple.swift
// Minimal Swift executable for testing symbol hooking
// Contains @_cdecl exported function AND pure Swift internal functions

import Foundation

@_cdecl("swift_test_cdecl")
public func swiftTestCdecl() -> Int32 {
    return testInternalSwift()
}

func testInternalSwift() -> Int32 {
    // Force actual Swift runtime usage to ensure this isn't optimized away
    let arr = [1, 2, 3]
    return Int32(arr.reduce(0, +))
}

@main
struct TestSwiftSimple {
    static func main() {
        print("Result: \(swiftTestCdecl())")
    }
}
