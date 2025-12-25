# Security Policy

## Supported Versions

We actively support the latest version of preheat with security updates.

| Version | Supported          |
| ------- | ------------------ |
| 0.1.x   | :white_check_mark: |
| < 0.1   | :x:                |

## Security Considerations

Preheat runs as root and has access to:
- `/proc` filesystem
- System memory
- File system for readahead operations

**Systemd Security Hardening:**
The default systemd service includes:
- `NoNewPrivileges=yes`
- `ProtectSystem=strict`
- `ProtectHome=yes`
- Restricted write paths

**State File Security:**
- State files are created with 0600 permissions
- O_NOFOLLOW prevents symlink attacks
- CRC32 integrity checking (v0.1.1+)

## Known Security Features

- Integer overflow fixes (v0.1.2)
- No unsafe string operations
- All file I/O operations validated
- Graceful handling of /proc failures
