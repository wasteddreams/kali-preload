# Fix Issue #1: Proc Monitoring Not Counting New Launches

## Progress Tracking

> [!IMPORTANT]
> **Maintain progress in `temp.md`** throughout implementation. Update after each significant step.

## Problem Analysis

**Symptom:** Launching Firefox 10+ times doesn't increase the "Raw Launches" counter.

**Root Causes Identified:**

### 1. Exit-Based Counting (Primary Issue)
The current implementation only increments `raw_launches` in `track_process_exit()` when a process **exits**. Long-running apps get no visibility until they close.

### 2. No Incremental Weight Updates
Weighted scores only calculated on exit. A Firefox session running for hours contributes nothing to predictions until closed.

### 3. Process Reuse by Modern Applications
Firefox/Chrome reuse processes for new windows — can't be detected via `/proc` (would need D-Bus/X11 integration).

---

## Recommended Solution: Approach A+ (Hybrid Counting with Incremental Updates)

Enhanced version that addresses all fixable issues:

### Key Changes

1. **Count `raw_launches` at process START** (immediate visibility)
2. **Update `weighted_launches` incrementally** each scan cycle for running processes
3. **Final adjustment on EXIT** to ensure total weight is accurate

### Weight Accumulation Logic

```
Each scan cycle (every 90 seconds):
  For each running process:
    elapsed_since_last_update = now - last_weight_update_time
    incremental_weight = calculate_weight(elapsed_since_last_update)
    exe->weighted_launches += incremental_weight
    proc_info->last_weight_update_time = now

On process exit:
  (Weight already accumulated incrementally, no final adjustment needed)
```

This means:
- Firefox running for 2 hours will accumulate weight progressively
- Predictions improve every 90 seconds, not just on exit
- `preheat-ctl explain firefox` shows current accumulated weight

---

## Proposed Changes

---

### [MODIFY] [spy.c](file:///home/lostproxy/Documents/Experiment/kalipreload/src/monitor/spy.c)

#### 1. Add `last_weight_update` field to process_info_t

In `spy.h` or at the top of `spy.c`:

```diff
 typedef struct {
     pid_t pid;
     pid_t parent_pid;
     time_t start_time;
+    time_t last_weight_update;  /* For incremental weight calculation */
     gboolean user_initiated;
 } process_info_t;
```

#### 2. Update `track_process_start()` — count immediately + init timestamp

```diff
 static void
 track_process_start(kp_exe_t *exe, pid_t pid, pid_t parent_pid)
 {
     process_info_t *proc_info;
+    time_t now = time(NULL);
     
     g_return_if_fail(exe);
     g_return_if_fail(exe->running_pids);
     
     if (g_hash_table_lookup(exe->running_pids, GINT_TO_POINTER(pid)))
         return;
     
     proc_info = g_new0(process_info_t, 1);
     proc_info->pid = pid;
     proc_info->parent_pid = parent_pid;
-    proc_info->start_time = time(NULL);
+    proc_info->start_time = now;
+    proc_info->last_weight_update = now;
     proc_info->user_initiated = is_user_initiated(parent_pid);
     
+    /* Increment raw launch count immediately */
+    exe->raw_launches++;
+    g_debug("Launch detected: %s (pid %d, user: %s)",
+            exe->path, pid, proc_info->user_initiated ? "yes" : "no");
+    
     g_hash_table_insert(exe->running_pids, GINT_TO_POINTER(pid), proc_info);
 }
```

#### 3. Add new function `update_running_weights()` — incremental updates

```c
/**
 * Update weighted_launches for all currently running processes
 * Called each scan cycle to provide incremental weight accumulation.
 */
static void
update_weight_for_pid(gpointer key, gpointer value, gpointer user_data)
{
    pid_t pid = GPOINTER_TO_INT(key);
    process_info_t *proc_info = (process_info_t *)value;
    kp_exe_t *exe = (kp_exe_t *)user_data;
    time_t now = time(NULL);
    time_t elapsed;
    double incremental_weight;
    
    (void)pid;  /* Unused */
    
    elapsed = now - proc_info->last_weight_update;
    if (elapsed <= 0)
        return;  /* No time passed, skip */
    
    /* Calculate weight for this interval */
    incremental_weight = calculate_launch_weight(elapsed, proc_info->user_initiated);
    
    /* Accumulate (but don't double-count the base weight) */
    exe->weighted_launches += incremental_weight;
    proc_info->last_weight_update = now;
}

static void
update_running_weights(kp_exe_t *exe)
{
    g_return_if_fail(exe);
    g_return_if_fail(exe->running_pids);
    
    g_hash_table_foreach(exe->running_pids, update_weight_for_pid, exe);
}
```

#### 4. Modify `track_process_exit()` — no more duplicate counting

```diff
 static void
 track_process_exit(kp_exe_t *exe, pid_t pid)
 {
     process_info_t *proc_info;
-    time_t now, duration;
-    double weight;
     
     g_return_if_fail(exe);
     g_return_if_fail(exe->running_pids);
     
     proc_info = g_hash_table_lookup(exe->running_pids, GINT_TO_POINTER(pid));
     if (!proc_info)
         return;
     
-    now = time(NULL);
-    duration = now - proc_info->start_time;
-    if (duration < 0)
-        duration = 0;
-    
-    weight = calculate_launch_weight(duration, proc_info->user_initiated);
-    
-    exe->weighted_launches += weight;
-    exe->raw_launches++;
+    /* Weight already accumulated incrementally via update_running_weights() */
+    /* Just update duration tracking */
+    time_t now = time(NULL);
+    time_t duration = now - proc_info->start_time;
+    if (duration < 0) duration = 0;
+    
     exe->total_duration_sec += (unsigned long)duration;
     
     g_hash_table_remove(exe->running_pids, GINT_TO_POINTER(pid));
 }
```

#### 5. Call `update_running_weights()` in `kp_spy_scan()`

```diff
 void
 kp_spy_scan(gpointer data)
 {
     state_changed_exes = new_running_exes = NULL;
     new_exes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
 
     kp_proc_foreach(running_process_callback_wrapper, data);
     kp_state->last_running_timestamp = kp_state->time;
 
     g_slist_foreach(kp_state->running_exes, already_running_exe_callback_wrapper, data);
 
-    /* Clean up exited processes for weighted tracking */
+    /* Update weights for running processes, then clean up exited ones */
     GHashTableIter iter;
     gpointer key, value;
     g_hash_table_iter_init(&iter, kp_state->exes);
     while (g_hash_table_iter_next(&iter, &key, &value)) {
         kp_exe_t *exe = (kp_exe_t *)value;
+        update_running_weights(exe);  /* Incremental weight update */
         clean_exited_pids(exe);
     }
 
     g_slist_free(kp_state->running_exes);
     kp_state->running_exes = new_running_exes;
 }
```

---

### [MODIFY] [spy.h](file:///home/lostproxy/Documents/Experiment/kalipreload/src/monitor/spy.h)

Add the `last_weight_update` field to the struct if it's defined there:

```diff
 typedef struct {
     pid_t pid;
     pid_t parent_pid;
     time_t start_time;
+    time_t last_weight_update;
     gboolean user_initiated;
 } process_info_t;
```

---

## Verification Plan

### Automated Tests

1. **Start daemon in debug mode:**
   ```bash
   sudo preheat -f -d
   ```

2. **Launch Firefox, wait, check weight accumulates:**
   ```bash
   firefox-esr &
   sleep 30  # Wait for 1.5 scan cycles
   preheat-ctl explain firefox-esr
   # Check: Raw Launches should be 1, weighted_launches > 0
   
   sleep 60  # Wait more
   preheat-ctl explain firefox-esr
   # Check: weighted_launches should be HIGHER than before
   ```

3. **Verify raw count immediate:**
   ```bash
   gedit &
   sleep 2  # Don't wait for full cycle
   preheat-ctl explain gedit
   # Raw Launches should already be 1 (not 0)
   ```

4. **Check logs for detection:**
   ```bash
   sudo journalctl -u preheat | grep "Launch detected"
   ```

### Manual Verification

1. Clear state, restart daemon, launch apps, verify counts update progressively.

---

## Summary of Changes

| File | Change |
|------|--------|
| `spy.c` | Add `last_weight_update` to `process_info_t` |
| `spy.c` | `track_process_start()`: count raw_launches immediately |
| `spy.c` | New `update_running_weights()`: incremental weight each cycle |
| `spy.c` | `track_process_exit()`: remove duplicate counting |
| `spy.c` | `kp_spy_scan()`: call `update_running_weights()` |
| `spy.h` | Add field to struct if defined there |

---

## What This Won't Fix

> [!WARNING]
> Firefox opening new windows in an existing process still won't register as separate launches. That requires D-Bus/X11 integration (future enhancement).
