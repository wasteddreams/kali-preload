# State File Format Specification

**Version:** 1.0  
**File:** `/usr/local/var/lib/preheat/preheat.state`  
**Format:** Binary, little-endian  
**Encoding:** UTF-8 for strings

---

## Overview

The state file persists preheat's learned data between daemon restarts:
- Application registry (paths, launch counts, timestamps)
- Memory maps for each application
- Markov chain transition probabilities
- Application families/groups

**Design Goals:**
- Compact (typical: 156KB for 120 apps)
- Fast to load (<100ms)
- Version-tolerant (forward/backward compat)

---

## File Structure

```
┌──────────────────────────────────────┐
│          HEADER (32 bytes)           │
├──────────────────────────────────────┤
│       EXECUTABLES SECTION            │
│  (variable length)                   │
│                                      │
│  For each executable:                │
│    - Metadata                        │
│    - Memory maps                     │
│    - Statistics                      │
├──────────────────────────────────────┤
│      MARKOV CHAINS SECTION           │
│  (variable length)                   │
│                                      │
│  For each Markov node:               │
│    - Source app                      │
│    - Transitions → probabilities     │
├──────────────────────────────────────┤
│     APPLICATION FAMILIES SECTION     │
│  (variable length)                   │
├──────────────────────────────────────┤
│       BAD EXES SECTION               │
│  (list of unreadable paths)          │
├──────────────────────────────────────┤
│      FOOTER (CRC32 checksum)         │
└──────────────────────────────────────┘
```

---

## Header Format

**Size:** 32 bytes

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| 0x00 | 16 | char[16] | Magic: "PREHEAT_STATE" + version |
| 0x10 | 4 | uint32 | Format version (currently 1) |
| 0x14 | 8 | uint64 | Timestamp (Unix epoch seconds) |
| 0x1C | 4 | uint32 | Total preload time (seconds) |
| 0x20 | 4 | uint32 | Number of executables |
| 0x24 | 4 | uint32 | Number of Markov nodes |
| 0x28 | 4 | uint32 | Number of bad exes |
| 0x2C | 4 | uint32 | Reserved (must be 0) |

**Magic String:**
```
Offset 0x00: "PREHEAT_STATE_V1" (16 bytes, null-padded)
```

**Version History:**
- `V1`: Initial format (current)

---

## Executables Section

For each executable entry:

### Executable Header

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Path length (N bytes) |
| +0x02 | N | char[N] | Executable path (UTF-8, NOT null-terminated) |
| +N | 8 | uint64 | File size (bytes) |
| +N+8 | 8 | uint64 | Last seen timestamp |
| +N+16 | 8 | uint64 | Raw launch count |
| +N+24 | 8 | double | Weighted launch count |
| +N+32 | 8 | uint64 | Total runtime (seconds) |
| +N+40 | 4 | uint32 | Number of maps (M) |
| +N+44 | 1 | uint8 | Pool type (0=obs, 1=priority) |
| +N+45 | 3 | - | Padding (reserved) |

### Memory Maps (M entries)

For each map in this executable:

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Map path length (P bytes) |
| +0x02 | P | char[P] | Map file path (UTF-8) |
| +P | 8 | uint64 | Offset in file |
| +P+8 | 8 | uint64 | Length (bytes) |
| +P+16 | 8 | double | Probability score |

**Example:**
```
Executable: /usr/bin/firefox
  Map 1: /usr/lib/firefox/libxul.so (offset 0, length 95MB, prob 0.85)
  Map 2: /lib/x86_64-linux-gnu/libc.so.6 (offset 0, length 1.8MB, prob 0.92)
```

---

## Markov Chains Section

For each Markov node:

### Markov Node Header

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Source app path length (N) |
| +0x02 | N | char[N] | Source app path |
| +N | 4 | uint32 | Number of transitions (T) |

### Transitions (T entries)

For each transition from source app:

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Target app path length (M) |
| +0x02 | M | char[M] | Target app path |
| +M |4 | uint32 | Transition count |
| +M+4 | 8 | double | Probability (count/total) |

**Example:**
```
Source: /usr/bin/firefox
  → /usr/bin/code (count=45, prob=0.32)
  → /usr/bin/terminal (count=89, prob=0.64)
  → [other] (count=6, prob=0.04)
```

---

## Application Families Section

For each family:

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Family name length (N) |
| +0x02 | N | char[N] | Family name (e.g., "browsers") |
| +N | 4 | uint32 | Member count (M) |

Then M member paths:

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Path length (P) |
| +0x02 | P | char[P] | Member executable path |

---

## Bad Exes Section

List of paths that couldn't be read (permissions, deleted files, etc.):

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 4 | uint32 | Count of bad exes (B) |

Then B entries:

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| +0x00 | 2 | uint16 | Path length (N) |
| +0x02 | N | char[N] | Bad exe path |

---

## Footer

| Offset | Size | Type | Description |
|--------|------|------|-------------|
| -0x04 | 4 | uint32 | CRC32 checksum of entire file (excluding this field) |

**CRC Algorithm:** CRC-32 (IEEE 802.3 polynomial)

---

## Data Types

| Type | Size | Description |
|------|------|-------------|
| uint8 | 1 byte | Unsigned 8-bit integer |
| uint16 | 2 bytes | Unsigned 16-bit integer (little-endian) |
| uint32 | 4 bytes | Unsigned 32-bit integer (little-endian) |
| uint64 | 8 bytes | Unsigned 64-bit integer (little-endian) |
| double | 8 bytes | IEEE 754 double-precision float |
| char[N] | N bytes | UTF-8 encoded string (NOT null-terminated) |

---

## Version Compatibility

### Forward Compatibility

Older daemon reading newer state file:
- Checks `format version` in header
- If version > supported: **discard state, start fresh**
- Logs warning: "State file from newer version"

### Backward Compatibility

Newer daemon reading older state file:
- Attempts to parse V1 format
- Missing fields filled with defaults
- Deprecated fields ignored

**Version Migration:**
When format changes:
1. Increment version number
2. Document changes in CHANGELOG
3. Provide migration tool if needed

---

## Corruption Handling

### Detection

1. **Magic check:** First 16 bytes must be "PREHEAT_STATE_V1"
2. **CRC32 check:** Recompute CRC, compare with footer
3. **Sanity checks:**
   - Counts (exes, maps) must be reasonable (<100k)
   - Paths must not exceed PATH_MAX (4096)
   - Timestamps must be recent (within 10 years)

### Recovery

If corrupted:
```c
if (corrupted) {
    log_warning("State file corrupted: %s", error);
    log_info("Discarding corrupt state, starting fresh");
    unlink(state_file);
    return;  // Continue with empty state
}
```

**User impact:** Loses history, must re-learn patterns

---

## Example State File

**Hex dump of minimal state file:**

```
00000000: 50 52 45 48 45 41 54 5f  53 54 41 54 45 5f 56 31  |PREHEAT_STATE_V1|
00000010: 01 00 00 00 00 00 00 00  a4 3e 8f 67 00 00 00 00  |.........>.g....|
00000020: 02 00 00 00 01 00 00 00  00 00 00 00 00 00 00 00  |................|
                                   ^^-- 2 exes, 1 Markov node

# Executable 1: /usr/bin/vim (13 bytes)
00000030: 0d 00 2f 75 73 72 2f 62  69 6e 2f 76 69 6d 00 10  |../usr/bin/vim..|
          ^^^^^ path_len=13
                ^^^^^^^^^^^^^^^^^^^^  path="/usr/bin/vim"
00000040: 42 00 00 00 00 00 00 00  ...
          ^^^^^^^^^^^^^^^^^^^^  size=4.2MB
...
```

---

## Tools

### Inspect State File

```bash
# View human-readable dump
sudo preheat-ctl dump

# Export to JSON (future)
sudo preheat-ctl export --json > state.json

# Hex dump for debugging
hexdump -C /usr/local/var/lib/preheat/preheat.state | head -50
```

### Validate State File

```bash
# Check CRC32
sudo preheat-ctl validate state
```

### Reset State

```bash
# Delete and restart fresh
sudo systemctl stop preheat
sudo rm /usr/local/var/lib/preheat/preheat.state
sudo systemctl start preheat
```

---

## Implementation Notes

### Writing State File

```c
// Pseudocode
FILE *f = fopen(tmp_path, "wb");

write_header(f, num_exes, num_markov);

for each exe:
    write_uint16(f, strlen(exe->path));
    fwrite(f, exe->path, strlen(exe->path));
    write_uint64(f, exe->size);
    write_uint64(f, exe->last_seen);
    write_uint64(f, exe->raw_launches);
    write_double(f, exe->weighted_launches);
    // ... maps ...

for each markov_node:
    // ... transitions ...

// Footer
uint32 crc = compute_crc32(file_contents);
write_uint32(f, crc);

fclose(f);
rename(tmp_path, real_path);  // Atomic replace
```

### Reading State File

```c
// Pseudocode
FILE *f = fopen(state_path, "rb");

if (!validate_magic(f)) {
    error("Invalid magic");
    return;
}

header = read_header(f);
if (header.version > CURRENT_VERSION) {
    warning("Future version, discarding");
    return;
}

for (i = 0; i < header.num_exes; i++) {
    exe = read_exe_entry(f);
    register_exe(exe);
}

// ... read rest ...

if (!validate_crc32(f)) {
    error("CRC mismatch, file corrupted");
    return;
}
```

---

## Security Considerations

### File Permissions

```bash
-rw------- 1 root root 156K /usr/local/var/lib/preheat/preheat.state
```

**Must be 600 (owner-only):** Contains user behavior history

### Tampering Detection

- CRC32 checksum prevents accidental corruption
- **Not cryptographically secure** - intentional modification possible
- If root compromised, state file least concern

### Privacy

State file reveals:
- All apps ever launched
- Usage patterns
- Temporal relationships

**Mitigation:** Encrypted disk recommended for sensitive environments

---

## Future Enhancements

Planned for V2 format:

1. **Compression:** gzip compressed sections (50% size reduction)
2. **Encryption:** Optional AES-256 encryption with key from `/etc/preheat.key`
3. **Delta encoding:** Store only changes since last save
4. **Pruning metadata:** Track "last significant" to auto-expire old apps

---

## References

- IEEE 802.3 CRC-32: https://en.wikipedia.org/wiki/Cyclic_redundancy_check
- UTF-8 spec: https://tools.ietf.org/html/rfc3629
- Little-endian: https://en.wikipedia.org/wiki/Endianness
