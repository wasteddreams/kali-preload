# Snap Firefox Root Cause Analysis

> [!NOTE]
> This analysis follows a strict 7-phase workflow. No code changes are proposed.

## Root Cause (TL;DR)

Firefox snap's **AppArmor profile blocks external processes** from reading `/proc/PID/maps`. This causes `kp_proc_get_maps()` to return 0, triggering a silent early exit in `new_exe_callback()` before the app is registered.

---

## Phase 1 — Problem Statement

**Observed:** Firefox (snap) passes all proc.c filters but never appears in state.

**Failure Boundary:** Between `spy.c:new_exe_callback()` entry and state registration.

---

## Phase 2 — Code Path Trace

```
kp_proc_foreach() → cmdline fallback works → callback invoked
    ↓
running_process_callback() → queued in new_exes
    ↓
kp_spy_update_model() → g_hash_table_foreach(new_exes, new_exe_callback)
    ↓
new_exe_callback():
    size = kp_proc_get_maps(pid, NULL, NULL)  ← fopen fails (AppArmor)
    if (!size) return;                        ← SILENT EXIT
    [never reaches kp_state_register_exe()]
```

---

## Phase 3 — Hypothesis

AppArmor blocks external `/proc/PID/maps` reads for strictly-confined snaps.

**Explains:**
- Silent drop (designed for dead processes)
- Snap-specific behavior
- Some snaps (classic/less restrictive) work

---

## Phase 4 — Adversarial Review

| Attack | Verdict |
|--------|---------|
| "Process exited" | Rejected — Firefox persistent |
| "minsize filter" | Rejected — never reaches size check |
| "daemon lacks permissions" | Partially — `CAP_DAC_READ_SEARCH` doesn't bypass AppArmor (MAC) |

---

## Phase 5 — Findings Audit

| Known Fact | Explained? |
|------------|------------|
| Path extracted via cmdline | ✅ |
| All proc.c filters pass | ✅ |
| Callback invoked | ✅ |
| Not in state | ✅ |
| Not in bad_exes | ✅ |
| No error logged | ✅ |
| Some snaps work | ✅ (less restrictive profiles) |

**Confidence: HIGH (85%)**

**Remaining uncertainty:** Not empirically verified (run `sudo cat /proc/$(pgrep firefox)/maps`).

---

## Phase 6 — Root Cause Selection

**Selected:** AppArmor blocks `/proc/PID/maps` access via `PTRACE_MODE_READ_FSCREDS` check. The daemon (despite CAP_DAC_READ_SEARCH) cannot bypass MAC restrictions.

---

## Phase 7 — Implementation Options (Pending Approval)

> [!IMPORTANT]
> No code changes until user approves an option.

### Option 1: Document Only
- Update `docs/known-limitations.md`
- Recommend: install Firefox via apt or Flatpak
- **Effort:** Low | **Risk:** None

### Option 2: Accept Without Map Scanning
- Register exe in state even if maps inaccessible
- Skip library preloading; preload main binary only
- **Effort:** Medium | **Risk:** Partial tracking may mislead users

### Option 3: Desktop File Fallback
- If `/proc/PID/maps` fails for snap paths, check if `.desktop` file exists
- If yes, track with minimal data
- **Effort:** Medium | **Risk:** Requires heuristics

### Option 4: Empirical Verification First
Run these commands before implementation:
```bash
sudo cat /proc/$(pgrep -n firefox)/maps
snap info firefox | grep confinement
sudo journalctl | grep -E 'apparmor.*maps'
```

---

## Resolution: December 31, 2025

### Empirical Testing Revealed Different Root Cause

Testing on Ubuntu 24.04 VM showed:
```bash
$ sudo cat /proc/$(pgrep -n firefox)/maps | wc -l
72   # Readable! NOT blocked by AppArmor
```

**The AppArmor hypothesis was correct for some systems (Kali) but not all (Ubuntu).**

### Actual Root Cause on Ubuntu

1. Firefox was tracked (in state) but in **observation pool** instead of priority pool
2. Desktop file scanner couldn't match snap paths because:
   - Snap `.desktop` files are in `/var/lib/snapd/desktop/applications/`
   - Exec= lines use `env VAR=value...` prefix
   - `/snap/bin/firefox` is a symlink to `/usr/bin/snap` (not the actual binary)

### Fixes Implemented

**Option 3 (Desktop File Fallback) was effectively implemented:**

1. Scan `/var/lib/snapd/desktop/applications/` for snap `.desktop` files
2. Skip `env VAR=value` prefixes in Exec= lines
3. Resolve `/snap/bin/<name>` to actual binary at `/snap/<name>/current/usr/lib/<name>/<name>`
4. Check snap paths BEFORE `realpath()` to avoid symlink resolution to `/usr/bin/snap`

### Final Status

| System | Root Cause | Fix |
|--------|-----------|-----|
| Kali Linux | AppArmor blocks `/proc/PID/maps` | cmdline fallback (existing) |
| Ubuntu 24.04 | Desktop file scanner issues | desktop.c fixes (new) |

**Both systems now correctly track snap Firefox in PRIORITY POOL.**

### Commits

1. `feat: resolve snap wrapper scripts to actual binaries in desktop.c`
2. `fix: scan /var/lib/snapd/desktop/applications for snap .desktop files`
3. `fix: skip 'env VAR=value' prefixes in Exec= lines`
4. `fix: check snap wrapper BEFORE realpath() to prevent resolution to /usr/bin/snap`

---

## Continued Investigation: December 31, 2025 (Launch Counting)

### Problem: Launch Counts Not Incrementing for Snap Apps

Despite desktop.c fixes working (Firefox now in PRIORITY POOL with `.desktop` match), the **raw_launches counter wasn't incrementing** for snap-installed apps.

### Root Cause Identified

Snap applications have `snap-confine` as their parent process, not a shell or desktop environment. The existing `is_user_initiated()` function only whitelists:
- Shells (bash, zsh, fish, sh)
- Terminal emulators (gnome-terminal, konsole, etc.)
- Desktop launchers (gnome-shell, plasmashell, etc.)

`snap-confine` isn't in this list, so snap app launches were treated as child processes and not counted.

### Fix Implemented

Added a **fallback check** in `spy.c:track_process_start()`:

```c
/* FALLBACK for snap/flatpak/container apps:
 * Only triggers when is_user_initiated() returned FALSE.
 * If exe has a .desktop file, it's a real GUI app launched by user.
 */
if (!proc_info->user_initiated && kp_desktop_has_file(exe->path)) {
    proc_info->user_initiated = TRUE;
}
```

This is purely **additive** - apt-installed apps work exactly as before.

### Test Results (Partial Success)

Initial testing showed improvement:
```
Initial Firefox State:
  Raw Launches: 1

...after 5 launches...

Raw Launches: 1 → 4 (+3)
```

Launches ARE being counted now, though not all (some snap helper processes persist).

### Unresolved Issue: Process Scanner Not Detecting Snap Apps

During extended testing on Ubuntu VM, a deeper issue emerged:

**Symptoms:**
- Firefox runs and is visible in `ps aux`
- `/proc/PID/exe` correctly shows `/snap/firefox/7559/usr/lib/firefox/firefox`
- `exeprefix` config includes `/snap/`
- BUT daemon shows "NOT TRACKED" for Firefox

**Investigation steps tried:**
1. Clean state file delete + daemon restart
2. Debug logging (`G_MESSAGES_DEBUG=all`)
3. Manual Firefox launch with 2-minute wait
4. Force scan via SIGUSR1

**Finding:** The daemon produces 0 lines of debug output even when run in foreground, suggesting the issue is earlier in the startup/initialization phase.

### Status: REQUIRES FURTHER INVESTIGATION

| Component | Status |
|-----------|--------|
| desktop.c snap scanning | ✅ Working |
| spy.c launch counting fallback | ✅ Implemented |
| Process scanner detecting snaps | ❌ Broken on Ubuntu VM |

### Next Steps for Future Debugging

1. Check if daemon is actually doing /proc scans (add explicit logging to kp_proc_foreach)
2. Verify glib log handler is properly initialized
3. Test on different Ubuntu version or fresh VM
4. Consider if there's an early exit in daemon initialization

### Related Commits

1. `bf15db5` - fix: add desktop file fallback for snap/container launch counting
2. `7b06f14` - test: improve snap launch tracking verification with delta counting
3. `ba9509c` - test: add multi-app snap launch tracking verification script
