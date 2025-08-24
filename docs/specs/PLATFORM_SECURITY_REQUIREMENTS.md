# Platform Security Requirements Specification

## Overview

This document specifies platform-specific security requirements for ADA's tracing capabilities. Each platform has unique security models that affect how binaries can be traced, especially in remote sessions.

## macOS

### Code Signing Requirements

macOS enforces strict code signing requirements for dynamic instrumentation (Frida operations).

#### Certificate Types and Capabilities

| Certificate Type | Local Terminal | Cost | SSH Session | CI/CD | Distribution |
|-----------------|----------------|------|-------------|--------|--------------|
| None | ❌ | Free | ❌ | ❌ | ❌ |
| Ad-hoc (`-`) | ✅ | Free | ❌ | ❌ | ❌ |
| Apple Development | ✅ | Free* | ⚠️ | ❌ | ❌ |
| Apple Developer ID | ✅ | $99/year | ✅ | ✅ | ✅ |

*Free with Apple ID via Xcode

#### Required Entitlements

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.security.get-task-allow</key>
    <true/>
    <key>com.apple.security.cs.disable-library-validation</key>
    <true/>
</dict>
</plist>
```

#### Signing Process

```bash
# For paid Developer ID certificate
codesign --force --options runtime \
         --entitlements entitlements.plist \
         --sign "Developer ID Application: Your Name" \
         <binary>

# For ad-hoc signing (local only)
codesign --force --entitlements entitlements.plist --sign - <binary>
```

#### Error Detection and User Guidance

When tracing fails on macOS, the system should:

1. **Detect signature type**:
   ```bash
   codesign -dv <binary> 2>&1 | grep "Signature"
   ```

2. **Check SSH session**:
   ```bash
   [[ -n "$SSH_CLIENT" ]] && echo "SSH session detected"
   ```

3. **Display appropriate error message**:
   ```
   ERROR: Cannot trace binary over SSH connection
   
   Current signature: ad-hoc
   Required: Apple Developer ID certificate
   
   Solutions:
   1. Sign with Developer ID ($99/year):
      ./utils/sign_binary.sh <binary>
      
   2. Use local Terminal.app instead of SSH
   
   3. Deploy daemon mode (future release)
   
   See: docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md#macos
   ```

### System Integrity Protection (SIP)

- Cannot trace system binaries even with proper signing
- Must inform users this is a platform limitation, not a tool bug
- Affected binaries: `/usr/bin/*`, `/System/*`

### Developer Mode Requirement

As of macOS 13+:
- Developer mode must be enabled in Settings → Privacy & Security
- Required even for signed binaries
- One-time setup per machine

## Linux

### Ptrace Capabilities

Linux uses ptrace for process tracing, which requires:

#### Permission Models

| Method | Scope | Setup Required | Security Impact |
|--------|-------|----------------|-----------------|
| `CAP_SYS_PTRACE` | Process | `setcap` on binary | Minimal |
| ptrace_scope=0 | System-wide | `/proc/sys/kernel/yama/ptrace_scope` | High |
| sudo | Per-execution | User in sudoers | Medium |

#### Configuration

```bash
# Check current ptrace scope
cat /proc/sys/kernel/yama/ptrace_scope

# Values:
# 0 - Classic ptrace (permissive)
# 1 - Restricted ptrace (default on Ubuntu)
# 2 - Admin only
# 3 - Disabled
```

#### Error Detection and User Guidance

```
ERROR: Cannot attach to process (Permission denied)

Current ptrace_scope: 1 (restricted)

Solutions:
1. Run with sudo:
   sudo ./ada trace <binary>
   
2. Add CAP_SYS_PTRACE capability:
   sudo setcap cap_sys_ptrace=eip ./ada
   
3. Temporarily allow ptrace (requires root):
   echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope

See: docs/specs/PLATFORM_SECURITY_REQUIREMENTS.md#linux
```

### AppArmor/SELinux Considerations

- Some distributions have additional MAC (Mandatory Access Control)
- May need policy exceptions for tracing
- Check with: `aa-status` or `sestatus`

## Windows (Future)

### Code Signing Requirements

- Similar to macOS, Windows may require signing for certain operations
- Authenticode certificates for distribution
- Test signing for development

### Debug Privileges

- Requires `SeDebugPrivilege` for process attachment
- Administrator rights typically required

## Implementation Guidelines

### Error Message Framework

```rust
enum PlatformSecurityError {
    MacOSSignature {
        current: SignatureType,
        required: SignatureType,
        is_ssh: bool,
    },
    LinuxPtrace {
        scope: i32,
        errno: i32,
    },
    WindowsPrivilege {
        required: String,
    },
}

impl PlatformSecurityError {
    fn user_message(&self) -> String {
        // Return platform-specific guidance
    }
    
    fn remediation_steps(&self) -> Vec<String> {
        // Return ordered list of solutions
    }
}
```

### Detection Logic

```rust
fn check_platform_requirements(binary: &Path) -> Result<(), PlatformSecurityError> {
    match std::env::consts::OS {
        "macos" => check_macos_signing(binary),
        "linux" => check_linux_ptrace(),
        "windows" => check_windows_privileges(),
        _ => Ok(()),
    }
}
```

## Testing Requirements

### macOS Testing Matrix

- [ ] Local Terminal.app with ad-hoc signing
- [ ] Local Terminal.app with Developer ID
- [ ] SSH session with ad-hoc signing (should fail)
- [ ] SSH session with Developer ID (should succeed)
- [ ] CI/CD with ad-hoc + entitlements

### Linux Testing Matrix

- [ ] Default Ubuntu with ptrace_scope=1
- [ ] With CAP_SYS_PTRACE capability
- [ ] With sudo
- [ ] Under Docker container

## References

- [Apple: Code Signing Guide](https://developer.apple.com/library/archive/documentation/Security/Conceptual/CodeSigningGuide/)
- [Linux: Yama LSM Documentation](https://www.kernel.org/doc/Documentation/security/Yama.txt)
- [Frida: Platform-specific Notes](https://frida.re/docs/troubleshooting/)