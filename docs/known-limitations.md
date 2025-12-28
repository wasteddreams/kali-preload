# Known Limitations

This document describes current limitations, design constraints, and "won't fix" issues in Preheat.

---

## Process Detection Limitations

### 1. Process Reuse Not Detected

**Problem:** When Firefox (or Chrome, etc.) opens a new window, it reuses the existing process instead of spawning a new one.

**Impact:**
- New windows don't increment launch count
- Under-counts actual application usage
- Prediction model may under-weight frequently-windowed apps

**Example:**
```bash
# First Firefox launch
firefox               # ✅ Counted as launch

# Opening new window (Ctrl+N)
firefox -new-window   # ❌ NOT counted (reuses existing process)
```

**Workaround:** None without deep integration

**Requires:**
- D-Bus session monitoring, OR
- X11/Wayland window manager integration, OR
- Desktop environment hooks

**Status:** **Won't Fix** - too invasive for daemon simplicity

---

### 2. Multi-Process Apps Increment Per Process

**Problem:** Electron/Chromium apps spawn multiple processes (main, renderer, GPU, etc.), each incrementing the counter.

**Impact:**
- Raw launches over-count actual app starts
- VS Code opening once may show 8+ "launches"

**Mitigation:**
- `weighted_launches` uses logarithmic scaling, dampening the effect
- Short-lived helper processes get 0.3x penalty (Issue #2 fix)
- Net result: Acceptable inflation (~1.5-2x vs 8x)

**Example:**
```
$ pgrep -a code
12345 /usr/share/code/code (main process)
12346 /usr/share/code/code --type=renderer
12347 /usr/share/code/code --type=gpu-process
12348 /usr/share/code/code --type=utility
...

Each increments raw_launches, but weighted calculation handles it gracefully.
```

**Status:** **Acceptable** - working as designed

---

### 3. Interpreter Scripts Not Tracked Directly

**Problem:** Python/Bash scripts are tracked as `/usr/bin/python3` not the script itself.

**Impact:**
- Cannot distinguish between different Python scripts
- All Python usage lumped together

**Example:**
```bash
./my-backup-script.py    # Tracked as: /usr/bin/python3
./my-game.py             # Also tracked as: /usr/bin/python3
```

**Workaround:** Use shebang with specific interpreter or compile to binary

**Status:** **By Design** - scripts don't have stable paths

---

### 4. Snap Applications Not Fully Supported ⚠️

**Problem:** Ubuntu snap packages run in a strict AppArmor sandbox that blocks access to `/proc/PID/exe` for snap-confined processes, even for root.

**Impact:**
- Snap apps like Firefox, Chromium, or other snap-packaged applications may not be tracked
- The daemon cannot reliably detect or preload snap applications
- This affects Ubuntu 22.04+ where many default apps are snaps

**Technical Details:**
```
# Normal app (works fine):
$ readlink /proc/12345/exe
/usr/bin/firefox

# Snap app (blocked by AppArmor):
$ readlink /proc/67890/exe
readlink: /proc/67890/exe: Permission denied
```

**Workaround:**
- Install applications via `apt` instead of snap when possible:
  ```bash
  # Remove snap Firefox
  sudo snap remove firefox
  
  # Install apt version
  sudo add-apt-repository ppa:mozillateam/ppa
  sudo apt update
  sudo apt install firefox
  ```
- Use Flatpak instead (less restrictive sandboxing)
- Accept that snap apps won't be preloaded

**Status:** **Snap Security Limitation** - cannot be fixed without changes to snap/AppArmor

---

## Prediction Limitations

### 4. Cannot Predict First-Time Launches

**Problem:** Preheat learns from history; brand new applications have no data.

**Impact:**
- First launch always cold (no preloading)
- Takes 2-3 uses before pattern emerges

**Mitigation:**
- Manual apps list (Issue #3) bypasses this
- Smart seeding (v1.0+) scans desktop files on first run
- Session boot window preloads top 5 apps

**Status:** **Inherent to ML approach**

---

### 5. Unpredictable Workflows Not Helped

**Problem:** Random, one-off usage has no pattern to learn.

**Example:**
```
Day 1: Firefox → Gimp → Blender
Day 2: Terminal → Vim → GCC
Day 3: LibreOffice → Inkscape → Audacity

No patterns = No predictions
```

**Status:** **Out of Scope** - preheat optimizes repetitive workflows

---

## Storage Limitations

### 6. Symlink Canonicalization May Confuse Users

**Problem:** Apps launched via different symlinks are tracked under canonical path.

**Impact:**
- `/usr/bin/firefox` and `/usr/bin/x-www-browser` both resolve to same executable
- User may expect separate tracking

**Example:**
```bash
$ firefox         # Tracked as: /usr/lib/firefox-esr/firefox-esr
$ x-www-browser   # Also tracked as: /usr/lib/firefox-esr/firefox-esr
```

**Status:** **Working as Designed** - prevents duplicate tracking

---

### 7. Large State Files on Heavily-Used Systems

**Problem:** Tracking hundreds of apps with complex Markov chains grows state file.

**Impact:**
- State file can reach 1-2 MB on extreme systems
- Longer save/load times (~200ms vs ~50ms)

**Benchmarks:**
| Apps Tracked | State File Size | Load Time |
|--------------|-----------------|-----------|
| 50 | 80 KB | 30ms |
| 150 | 240 KB | 90ms |
| 500+ | 1.2 MB | 280ms |

**Mitigation:**
- Increase `minsize` to filter trivial apps
- Periodic state cleanup (future enhancement)

**Status:** **Acceptable** - rare edge case

---

## Memory Management Limitations

### 8. Cannot Force Kernel to Keep Cache

**Problem:** Kernel may evict preloaded data under memory pressure.

**Impact:**
- Preloading benefit lost if evicted before app launch
- No way to "pin" pages in cache

**Example:**
```
19:00 - Preheat preloads Firefox (120 MB)
19:05 - User compiles large project (needs RAM)
19:10 - Kernel evicts Firefox from cache
19:15 - User launches Firefox → COLD START (preload wasted)
```

**Mitigation:**
- Conservative memory budgets (default: 50% free, -10% total)
- Cache thrashing reduced but not eliminated

**Status:** **Linux Kernel Limitation** - by design

---

### 9. SSD Performance Gains Are Modest

**Problem:** SSDs (especially NVMe) are already fast; preloading shows minimal improvement.

**Reality Check:**

| Storage Type | Cold Start | With Preheat | Improvement |
|--------------|------------|--------------|-------------|
| **HDD (5400 RPM)** | 8.5s | 3.2s | **62%** ✅ |
| **SSD (SATA)** | 2.1s | 1.3s | **38%** ✅ |
| **NVMe** | 1.1s | 0.9s | **18%** ⚠️ |

**Recommendation:** Most beneficial on HDD systems

**Status:** **Physics Limitation** - fast storage needs less help

---

## Configuration Limitations

### 10. No Per-Application Memory Limits

**Problem:** Cannot limit preloading budget per app (e.g., "max 50 MB for games").

**Impact:**
- Large applications (Chrome: 200MB) can dominate budget
- Smaller apps may be starved

**Workaround:**
- Adjust global `minsize` threshold
- Use blacklist to exclude massive apps

**Status:** **Future Enhancement** - would add complexity

---

### 11. Prediction Threshold Not Auto-Tuning

**Problem:** Fixed 0.30 threshold for preloading; doesn't adapt to system load.

**Better Design:**
- Low memory → raise threshold (0.50)
- High memory → lower threshold (0.15)

**Status:** **Future Enhancement** - would require load-aware logic

---

## Security/Privacy Limitations

### 12. State File Reveals User Behavior

**Problem:** State file contains complete application history and patterns.

**Privacy Impact:**
- `/var/lib/preheat/preheat.state` shows:
  - Every app you've ever launched
  - Temporal relationships (Firefox → Bank Website pattern)
  - Activity times

**Mitigation:**
- File permissions: 600 (root only)
- Encrypted disk recommended
- No network exposure (local daemon only)

**Status:** **Inherent Trade-off** - learning requires history

---

## Platform Limitations

### 13. Linux-Only

**Problem:** Uses Linux-specific interfaces (`/proc`, `readahead(2)`).

**Status:** **By Design** - no Windows/macOS port planned

---

### 14. Requires Root (for readahead)

**Problem:** `readahead(2)` syscall requires root privileges.

**Workaround:** None - systemd runs daemon as root

**Status:** **Linux Kernel Requirement**

---

## Summary

| Category | Limitation | Severity | Status |
|----------|-----------|----------|--------|
| Detection | Process reuse not detected | Medium | Won't Fix |
| Detection | Multi-process over-counting | Low | Acceptable |
| **Detection** | **Snap apps not supported** | **High** | **Platform Limit** |
| Prediction | First-time apps | Low | Mitigated |
| Storage | Symlink canonicalization | Low | By Design |
| Memory | Kernel eviction | Medium | Unavoidable |
| Performance | NVMe gains modest | Low | Physics |
| Security | Privacy exposure | Medium | Documented |

---

## Reporting New Limitations

Found a limitation not listed here?

1. Check if it's a bug (unexpected) or constraint (expected)
2. If constraint: Open issue with "limitation" label
3. If bug: Open regular bug report

**Remember:** Not every constraint is fixable - some are fundamental trade-offs!
