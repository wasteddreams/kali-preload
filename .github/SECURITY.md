# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| 1.0.x   | :white_check_mark: |
| < 1.0   | :x:                |

## Security Architecture

Preheat is designed with security as a core principle. Despite running with elevated privileges for system optimization, it implements defense-in-depth measures to minimize attack surface.

### Privilege Model

- **Runs as root** - Required for `readahead()` syscalls and `/proc` access
- **No network access** - Daemon is completely offline, no sockets opened
- **Read-only operations** - Only reads files for preloading, never modifies user data
- **Minimal syscalls** - Limited to `readahead()`, `stat()`, `open()` for file operations

### Systemd Hardening

The default systemd service (`preheat.service`) includes comprehensive security restrictions:

```ini
# Privilege restrictions
NoNewPrivileges=yes
CapabilityBoundingSet=CAP_SYS_ADMIN CAP_DAC_READ_SEARCH

# Filesystem protection
ProtectSystem=strict
ProtectHome=read-only
PrivateTmp=yes
ReadWritePaths=/var/lib/preheat /var/log

# Kernel hardening
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectKernelLogs=yes

# Additional isolation
PrivateDevices=yes
ProtectHostname=yes
ProtectClock=yes
ProtectControlGroups=yes
RestrictRealtime=yes
RestrictSUIDSGID=yes
```

### File Security

| Feature | Description |
|---------|-------------|
| **State file permissions** | Created with `0600` (owner read/write only) |
| **O_NOFOLLOW** | Prevents symlink traversal attacks |
| **CRC32 integrity** | Detects corruption or tampering |
| **Path validation** | Rejects paths outside trusted directories |
| **Atomic writes** | State saved atomically to prevent corruption |

### Input Validation

- **Path sanitization** - All paths canonicalized with `realpath()`
- **URI handling** - Safe `g_filename_from_uri()` prevents injection
- **Config parsing** - Bounds checking on all numeric inputs
- **Buffer safety** - No unbounded string operations

### Memory Safety

- **GLib allocators** - Uses GLib's safe memory functions
- **No raw malloc** - All allocations via `g_new()`, `g_malloc()`
- **Cleanup patterns** - Consistent `goto cleanup` for resource management
- **Integer overflow protection** - Size calculations checked before use

## Reporting a Vulnerability

If you discover a security vulnerability:

1. **Do NOT open a public issue**
2. Email the maintainer directly with details
3. Include reproduction steps if possible
4. Allow 90 days for remediation before disclosure

We take all security reports seriously and will respond within 48 hours.

## Security Audit Status

Last audit: December 2025

| Category | Status |
|----------|--------|
| Memory safety | ✅ Reviewed |
| Input validation | ✅ Reviewed |
| Privilege escalation | ✅ Hardened |
| File operations | ✅ Secured |
| Network exposure | ✅ None |

## Known Limitations

- Daemon requires root privileges (unavoidable for `readahead()`)
- Log files may contain executable paths (not sensitive)
- State file not encrypted (contains only path metadata)
