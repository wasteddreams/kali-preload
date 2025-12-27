# Issue #1 Fix Progress

## Current Status: IMPLEMENTING ⚙️

**Last Updated:** 2025-12-27 17:51 IST

---

## Completed Steps

- [x] Analyzed root cause of launch counting issue
- [x] Identified 3 root causes (exit-based counting, no incremental updates, process reuse)
- [x] Evaluated 3 approaches (A, B, C)
- [x] Selected enhanced Approach A+ (Hybrid with Incremental Updates)
- [x] Committed refactoring changes to git
- [x] Created implementation plan
- [x] Examined current code structure
- [x] Located process_info_t definition in state.h

---

## Next Steps

- [x] Add last_weight_update to process_info_t
- [x] Implement update_running_weights() functions
- [x] Modify track_process_start() to count immediately
- [x] Modify track_process_exit() to remove duplicate counting
- [x] Update kp_spy_scan() to call incremental updates
- [x] Build and verify compilation ✅ (No errors)
- [x] Install updated daemon
- [x] Fix double-removal bug ✅
- [/] Test with vim - verifying if launch count increments
- [ ] Verify weight accumulation works incrementally
- [ ] Update CONTEXT.md
- [ ] Commit fix to git

---

## Git Log

```
02d27cc - refactor: Split large files for readability and maintainability (just now)
```

---

## Test Scenario

**Fresh State Test:**
- Cleared old state file (backed up as preheat.state.backup-20251227-180137)
- Daemon started with fresh state
- Baseline: `antigravity` has 26 raw launches, 34.4 weighted
- Launched antigravity (PID 170580)
- **RESULT after 25s:** `antigravity` now has **29 raw launches** (+3), **48.8 weighted** (+14.4)

✅ **SUCCESS:** Raw launches incremented immediately (not waiting for exit)
✅ **SUCCESS:** Weighted launches accumulated incrementally

---

## Key Files to Modify

1. `src/monitor/spy.c` - Main changes
2. `src/monitor/spy.h` - Add field to struct (if defined there)

---

## Notes

- Firefox process reuse (new tab = same PID) cannot be fixed without D-Bus/X11 integration
- Incremental weight updates will improve prediction accuracy for long-running apps
- **BUG FOUND & FIXED:** Double-removal in hash table - `track_process_exit()` tried to manually remove entry that was already being removed by `g_hash_table_foreach_remove()` callback
- The crash was: `GLib:ERROR: assertion failed: (hash_table->nnodes > 0)`
