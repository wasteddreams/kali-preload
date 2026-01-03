# Preheat Daemon Bug Audit

## 🔴 High Complexity (7/7 Complete)
| # | Feature | Status |
|---|---------|--------|
| 1 | Markov Chain Predictions | ✅ Audited & Fixed |
| 2 | App Family Detection | ✅ Audited & Fixed |
| 3 | Library Scanning | ✅ Audited & Fixed |
| 4 | State Persistence | ✅ Audited & Fixed |
| 5 | PID Persistence | ✅ Clean |
| 6 | Smart Seeding | ✅ Audited & Fixed |
| 7 | Hit/Miss Tracking | ✅ Audited & Fixed |

## 🟡 Medium Complexity (3/7 Audited)
| # | Feature | Status |
|---|---------|--------|
| 8 | Weighted Launch Counting | ✅ Audited & Fixed |
| 9 | Pool Classification | ✅ Clean |
| 10 | Session Boot Window | ✅ Audited & Fixed |
| 11 | Process Monitoring | 🔲 |
| 12 | Signal Handling | ✅ Clean |
| 13 | Memory Pressure Awareness | 🔲 |
| 14 | Desktop File Scanning | 🔲 |

## 🟢 Low Complexity (9/9 Complete)
| # | Feature | Status |
|---|---------|--------|
| 15 | explain command | ✅ Audited & Fixed |
| 16 | Readahead Preloading | ✅ Clean |
| 17 | stats command | ✅ Audited & Fixed |
| 18 | top command | ✅ Clean |
| 19 | pause/resume | ✅ Clean |
| 20 | promote/demote | ✅ Clean |
| 21 | save/reload commands | ✅ Clean |
| 22 | Autosave | ✅ Clean |
| 23 | Graceful Shutdown | ✅ Clean |

---

## Bugs Fixed (2026-01-02)

### Markov Chain Predictions (5 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| M-1 | `state_markov.c:69` | Wrong timestamp assignment in init | Medium |
| M-3 | `state_markov.c:102` | Assertion crash on same-state | Medium |
| M-4 | `state_markov.c:217` | Division by zero in correlation | Low |
| M-5 | `state.h:165` | `int time` overflows after 2y | Low |
| M-6 | `state_markov.c:82` | Memory leak on OOM | Low |

### App Family Detection (4 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| F-1 | `stats.c:578` | Use-after-free on `sorted->len` | High |
| F-2 | `config.c:875` | Families loaded before state init | Medium |
| F-3 | `state_io.c:460` | Duplicate family IDs leak memory | Medium |
| F-4 | `state_family.c:121` | `time_t` cast truncates after 2038 | Low |

### Library Scanning (5 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| **L-1** | `lib_scanner.c:88` | **Command injection via popen** | **CRITICAL** |
| L-2 | `proc.c:206` | Integer overflow if `end < start` | Medium |
| L-3 | `proc.c:184` | Memory leak on fopen failure | Low |
| L-4 | `proc.c:195` | Buffer not initialized | Low |
| L-5 | `proc.c:197` | Sign mismatch in length | Low |

### State Persistence (1 bug)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| S-1 | `state_io.c:729` | `%lu` for `size_t` truncates | Low |

### Smart Seeding (1 bug)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| SS-1 | `seeding.c:276` | `=` instead of `+=` clobbers data | Medium |

### Hit/Miss Tracking (1 bug)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| HM-1 | `stats.c:178` | strncpy → g_strlcpy for safety | Low |

### CLI Commands (2 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| C-1 | `ctl_cmd_apps.c:90` | Pool value inverted | Low |
| C-2 | `ctl_cmd_stats.c:362` | Wrong state file path | Low |

---

## Bugs Fixed (2026-01-03)

### Markov Chain Creation (2 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| MC-1 | `state_exe.c:193` | Chains only created for first 100 exes (B012 limit) | High |
| MC-2 | `seeding.c:*` | All seeding used `create_markovs=FALSE` | High |

**Root Cause:** Markov chains were never created because:
1. B012 fix limited chain creation to first 100 exes
2. Seeding functions all passed `FALSE` to `kp_state_register_exe()`
3. Chains created at registration, but exes registered in wrong order

**Fix Applied:**
- Added `kp_markov_build_priority_mesh()` in `state_markov.c` (lines 240-302)
- Builds chains between ALL priority pool apps after seeding completes
- Called from `main.c` after `kp_stats_reclassify_all()`
- Changed `state_exe.c:195` to only create chains for PRIORITY pool apps
- Updated all 8 seeding locations to pass `exe->pool == POOL_PRIORITY`

### Preload Stats Persistence (1 bug)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| PS-1 | `state_io.c` | Preload timestamps not persisted | Medium |

**Fix Applied:**
- Added `TAG_PRELOAD_TIMES` and `TAG_PRELOAD_TIME` tags
- Added `kp_stats_save_preload_times()` and `kp_stats_load_preload_time()` in `stats.c`
- Integrated into `kp_state_write_to_channel()` and `kp_state_read_from_channel()`

### Session Preload (1 bug)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| SP-1 | `session.c:435` | Map loading skipped when `exemaps` not empty | Medium |

**Root Cause:** Condition `g_set_size(exe->exemaps) == 0` too restrictive - apps with stale/empty map data weren't rescanned.

**Fix Applied:**
- Changed to `exe->size < (size_t)kp_conf->model.minsize`
- Forces map rescan if total preloaded size is suspiciously small

### Single-Instance Protection (New Feature)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| SI-1 | `main.c` | No protection against duplicate instances | Medium |

**Implemented:**
- Added `acquire_pidfile_lock()` with `flock(LOCK_EX|LOCK_NB)`
- PID file at `/var/run/preheat.pid`
- Added `release_pidfile_lock()` at shutdown
- Immediate exit with error message if another instance running

### CLI & Security Hardening (2 bugs)
| ID | File | Issue | Severity |
|----|------|-------|----------|
| CLI-1 | `ctl_cmd_apps.c:421` | show_hidden showed priority pool instead of observation | Low |
| SEC-1 | `lib_scanner.c:90` | ldd invoked without absolute path | Low |

**Fix Applied:**
- CLI-1: Changed `pool == 1` to `pool == 0`
- SEC-1: Changed `ldd` to `/usr/bin/ldd` for defense-in-depth

---

## Summary

| Tier | Features | Audited | Bugs | Fixed |
|------|----------|---------|------|-------|
| High | 7 | 7 | 17 | 17 |
| Medium | 7 | 3 | 4 | 4 |
| Low | 9 | 9 | 4 | 4 |
| **Total** | **23** | **19** | **25** | **25** |

**Critical Fixes:**
- Command injection in `lib_scanner.c` - malicious filenames could execute shell commands
- Markov chain mesh builder - predictions now work correctly after seeding
- Single-instance protection - prevents duplicate daemon processes
- `show-hidden` pool check - now correctly shows observation pool apps
