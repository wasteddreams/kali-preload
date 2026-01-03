#!/bin/bash
# test_markov_prediction.sh - Test Markov chain prediction and preloading
#
# This script runs in background, simulating app usage and monitoring
# prediction/preload behavior over time.
#
# Usage: ./scripts/test_markov_prediction.sh &
# Output: ./markov_test.log

set -e

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LOG_FILE="$PROJECT_DIR/markov_test.log"
STATE_FILE="/usr/local/var/lib/preheat/preheat.state"
DAEMON_LOG="/usr/local/var/log/preheat.log"
TEST_DURATION_MINUTES=10
SAMPLE_INTERVAL_SEC=20

# Apps to simulate launching (common GUI apps)
TEST_APPS=(
    "/usr/bin/firefox-esr"
    "/usr/bin/gnome-terminal"
    "/usr/bin/nautilus"
    "/usr/bin/gedit"
    "/usr/bin/gnome-calculator"
)

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_FILE"
}

error() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: $*" | tee -a "$LOG_FILE" >&2
}

# Clear previous log
echo "=== Markov Prediction Test Started ===" > "$LOG_FILE"
echo "Test duration: $TEST_DURATION_MINUTES minutes" >> "$LOG_FILE"
echo "Sample interval: $SAMPLE_INTERVAL_SEC seconds" >> "$LOG_FILE"
echo "" >> "$LOG_FILE"

log "Starting Markov prediction test..."
log "Project: $PROJECT_DIR"
log "Log file: $LOG_FILE"

# Initial state
log ""
log "=== INITIAL STATE ==="
log "Markov chains: $(sudo grep -c '^MARKOV' "$STATE_FILE" 2>/dev/null || echo 0)"
log "MAPs: $(sudo grep -c '^MAP' "$STATE_FILE" 2>/dev/null || echo 0)"
log "EXEs: $(sudo grep -c '^EXE' "$STATE_FILE" 2>/dev/null || echo 0)"

log ""
log "Initial preheat-ctl stats:"
sudo preheat-ctl stats 2>&1 | tee -a "$LOG_FILE"

# Calculate end time
END_TIME=$(($(date +%s) + TEST_DURATION_MINUTES * 60))
SAMPLE_NUM=0

log ""
log "=== MONITORING LOOP ==="
log "Will sample every $SAMPLE_INTERVAL_SEC seconds for $TEST_DURATION_MINUTES minutes..."

while [ $(date +%s) -lt $END_TIME ]; do
    SAMPLE_NUM=$((SAMPLE_NUM + 1))
    
    log ""
    log "--- Sample #$SAMPLE_NUM at $(date '+%H:%M:%S') ---"
    
    # Get stats
    STATS=$(sudo preheat-ctl stats 2>&1)
    PRELOADS=$(echo "$STATS" | grep "Total:" | awk '{print $2}' || echo "N/A")
    HITS=$(echo "$STATS" | grep "Hits:" | awk '{print $2}' || echo "N/A")
    MISSES=$(echo "$STATS" | grep "Misses:" | awk '{print $2}' || echo "N/A")
    HIT_RATE=$(echo "$STATS" | grep "Hit Rate:" | awk '{print $3}' || echo "N/A")
    
    log "Preloads: $PRELOADS, Hits: $HITS, Misses: $MISSES, Hit Rate: $HIT_RATE"
    
    # Check Markov state from log
    MARKOV_COUNT=$(sudo grep -c "^MARKOV" "$STATE_FILE" 2>/dev/null || echo 0)
    log "Markov chains in state: $MARKOV_COUNT"
    
    # Check if readahead happened (from daemon log)
    RECENT_READAHEAD=$(sudo tail -50 "$DAEMON_LOG" 2>/dev/null | grep -c "readahead" || echo 0)
    RECENT_PREDICT=$(sudo tail -50 "$DAEMON_LOG" 2>/dev/null | grep -c "dopredict" || echo 0)
    log "Recent readahead entries: $RECENT_READAHEAD, predict cycles: $RECENT_PREDICT"
    
    # Occasionally launch a test app briefly (every 3rd sample)
    if [ $((SAMPLE_NUM % 3)) -eq 0 ]; then
        # Pick a random app
        APP_IDX=$((RANDOM % ${#TEST_APPS[@]}))
        APP="${TEST_APPS[$APP_IDX]}"
        
        if [ -x "$APP" ]; then
            log "Launching test app: $APP (will close in 3s)"
            timeout 3 "$APP" >/dev/null 2>&1 &
            APP_PID=$!
            sleep 3
            kill $APP_PID 2>/dev/null || true
            wait $APP_PID 2>/dev/null || true
            log "Test app closed"
        else
            log "Test app not found: $APP"
        fi
    fi
    
    sleep $SAMPLE_INTERVAL_SEC
done

log ""
log "=== FINAL STATE ==="
log "Markov chains: $(sudo grep -c '^MARKOV' "$STATE_FILE" 2>/dev/null || echo 0)"

log ""
log "Final preheat-ctl stats:"
sudo preheat-ctl stats -v 2>&1 | tee -a "$LOG_FILE"

log ""
log "Recent daemon log entries:"
sudo tail -30 "$DAEMON_LOG" 2>&1 | grep -iE "readahead|preload|predict|mesh|chain|kb" | tee -a "$LOG_FILE"

log ""
log "=== TEST COMPLETED ==="
log "Total samples: $SAMPLE_NUM"
log "Log saved to: $LOG_FILE"
