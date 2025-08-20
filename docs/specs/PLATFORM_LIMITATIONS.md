# Platform Limitations

This document describes platform-specific limitations and constraints for the ADA tracer. As a multi-platform, multi-language debugging tool, understanding these limitations is crucial for proper usage.

## macOS

### System Binary Restrictions

**Limitation**: Cannot attach to or trace system binaries when System Integrity Protection (SIP) is enabled.

**Affected Paths**:
- `/System/*`
- `/bin/*`
- `/sbin/*`
- `/usr/bin/*` (except `/usr/local/bin/*`)
- Applications signed by Apple

**Reason**: macOS System Integrity Protection prevents debugging tools from accessing system processes, even with:
- Administrator privileges
- Developer ID certificates
- All debugging entitlements
- Code signing

**User Guidance**: 
- Do not attempt to trace system binaries on macOS
- Use your own compiled binaries or development builds instead
- Place test binaries in user-controlled directories (e.g., `~/bin/`, project directories)

**Error Message Example**:
```
Cannot trace system binary '/bin/ls' on macOS.
System binaries are protected by System Integrity Protection.
Please use your own binaries or development builds instead.
```

### Required Permissions

**Developer Tools Access**:
- Required for: Attaching to processes
- How to enable: System Settings → Privacy & Security → Developer Tools → Enable for Terminal

**Code Signing**:
- Required for: Spawning and attaching to processes
- Requirements: Valid Developer ID or ad-hoc signature with debugging entitlements

## Linux

### Ptrace Restrictions

**Limitation**: Cannot attach to processes without appropriate ptrace permissions.

**Configuration**: Check `/proc/sys/kernel/yama/ptrace_scope`
- `0`: Classic ptrace (permissive)
- `1`: Restricted ptrace (default on Ubuntu)
- `2`: Admin-only attach
- `3`: No attach

**User Guidance**:
- For development, consider setting `ptrace_scope` to 0
- Use `CAP_SYS_PTRACE` capability for production environments
- Run with appropriate user permissions

### Container Environments

**Limitation**: Additional restrictions in Docker/container environments.

**Requirements**:
- `--cap-add=SYS_PTRACE` for ptrace capabilities
- `--security-opt seccomp=unconfined` for full syscall access
- Host PID namespace access for cross-container tracing

## Windows

### Protected Processes

**Limitation**: Cannot attach to protected Windows system processes.

**Affected Processes**:
- System-critical services
- Anti-malware processes
- DRM-protected applications

**User Guidance**:
- Focus on user-mode applications
- Use development builds of your software
- Run with Administrator privileges when needed

### Code Signing

**Requirements**:
- Kernel-mode code must be signed with EV certificate
- User-mode typically doesn't require signing for debugging

## Cross-Platform Guidelines

### Universal Restrictions

Regardless of platform, the ADA tracer cannot:
1. Trace kernel-mode code without special privileges
2. Bypass platform security features
3. Attach to protected/encrypted processes
4. Trace across security boundaries without elevation

### Best Practices

1. **Always use development builds** for comprehensive tracing
2. **Place binaries in user directories** to avoid permission issues
3. **Test with appropriate privileges** for your use case
4. **Respect platform security models** - they exist for good reasons

### Testing Approach

For consistent cross-platform testing:
1. Use custom test binaries (not system utilities)
2. Place test binaries in project directories
3. Sign binaries appropriately for each platform
4. Document platform-specific test requirements

## Platform Detection

The tracer will automatically detect platform limitations and provide appropriate guidance:

```json
{
  "platform": "macos",
  "limitation_detected": "system_binary",
  "target": "/bin/echo",
  "message": "Cannot trace system binary on macOS",
  "suggestion": "Use custom binaries in user directories"
}
```

## Future Considerations

As platforms evolve, new restrictions may emerge:
- iOS/iPadOS debugging requirements
- Android SELinux policies
- WebAssembly sandbox limitations
- Container orchestration constraints

This document will be updated as new platform limitations are discovered and documented.