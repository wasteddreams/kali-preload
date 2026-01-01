#!/bin/bash
# benchmark.sh - Preheat Daemon Performance Benchmark
# 
# Measures application startup times with and without preloading
# to quantify the improvement from the preheat daemon.
#
# Usage: sudo ./benchmark.sh [app_name]
# Example: sudo ./benchmark.sh firefox-esr

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Configuration
ITERATIONS=3
WARMUP_SLEEP=2
MEASURE_TIMEOUT=30

# Apps to benchmark (name:command:window_class)
declare -A APPS=(
    ["firefox"]="/usr/lib/firefox-esr/firefox-esr:firefox-esr:firefox"
    ["calculator"]="/usr/bin/gnome-calculator:gnome-calculator:gnome-calculator"
    ["nautilus"]="/usr/bin/nautilus:nautilus:org.gnome.Nautilus"
    ["gedit"]="/usr/bin/gedit:gedit:gedit"
    ["gnome-terminal"]="/usr/bin/gnome-terminal:gnome-terminal:gnome-terminal"
)

log() { echo -e "${BLUE}[BENCH]${NC} $1"; }
success() { echo -e "${GREEN}[OK]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        error "This script must be run as root (sudo)"
        exit 1
    fi
}

# Check dependencies
check_deps() {
    for cmd in preheat-ctl xdotool vmtouch; do
        if ! command -v $cmd &>/dev/null; then
            warn "$cmd not found - some features may not work"
        fi
    done
}

# Drop page cache to simulate cold start
drop_caches() {
    sync
    echo 3 > /proc/sys/vm/drop_caches
    sleep 1
}

# Check if window appeared (returns time in ms)
wait_for_window() {
    local window_class="$1"
    local start_time=$(date +%s%3N)
    local timeout_ms=$((MEASURE_TIMEOUT * 1000))
    
    while true; do
        local elapsed=$(($(date +%s%3N) - start_time))
        if [[ $elapsed -gt $timeout_ms ]]; then
            echo "-1"
            return
        fi
        
        if xdotool search --class "$window_class" &>/dev/null; then
            echo "$elapsed"
            return
        fi
        sleep 0.05
    done
}

# Kill app completely
kill_app() {
    local cmd="$1"
    local basename=$(basename "$cmd")
    pkill -9 -f "$basename" 2>/dev/null || true
    sleep 1
}

# Measure single app startup time
measure_startup() {
    local app_path="$1"
    local app_cmd="$2"
    local window_class="$3"
    local desc="$4"
    
    kill_app "$app_cmd"
    
    local start_time=$(date +%s%3N)
    $app_cmd &>/dev/null &
    local pid=$!
    
    local elapsed=$(wait_for_window "$window_class")
    
    kill_app "$app_cmd"
    
    if [[ "$elapsed" == "-1" ]]; then
        echo "timeout"
    else
        echo "$elapsed"
    fi
}

# Run benchmark for one app
benchmark_app() {
    local app_name="$1"
    local app_info="${APPS[$app_name]}"
    
    if [[ -z "$app_info" ]]; then
        error "Unknown app: $app_name"
        error "Available: ${!APPS[@]}"
        return 1
    fi
    
    IFS=':' read -r app_path app_cmd window_class <<< "$app_info"
    
    echo ""
    echo "═══════════════════════════════════════════════════════════"
    echo " Benchmarking: $app_name"
    echo " Binary: $app_path"
    echo "═══════════════════════════════════════════════════════════"
    
    # Check if binary exists
    if [[ ! -f "$app_path" ]]; then
        error "Binary not found: $app_path"
        return 1
    fi
    
    # Check cache status
    echo ""
    log "Checking initial cache status..."
    if command -v vmtouch &>/dev/null; then
        vmtouch -v "$app_path" 2>/dev/null | head -3 || true
    fi
    
    # Phase 1: Cold start (drop caches)
    echo ""
    log "Phase 1: COLD START (caches dropped)"
    local cold_times=()
    
    for i in $(seq 1 $ITERATIONS); do
        drop_caches
        systemctl stop preheat 2>/dev/null || true
        sleep $WARMUP_SLEEP
        
        local time=$(measure_startup "$app_path" "$app_cmd" "$window_class" "cold")
        if [[ "$time" != "timeout" ]]; then
            cold_times+=("$time")
            echo "  Run $i: ${time}ms"
        else
            echo "  Run $i: TIMEOUT"
        fi
    done
    
    # Phase 2: Warm start (with preheat)
    echo ""
    log "Phase 2: WARM START (preheat active)"
    systemctl start preheat 2>/dev/null || true
    sleep 5  # Let preheat do initial scan
    
    # Force preload by triggering prediction
    preheat-ctl save 2>/dev/null || true
    sleep 3
    
    local warm_times=()
    for i in $(seq 1 $ITERATIONS); do
        sleep $WARMUP_SLEEP
        
        local time=$(measure_startup "$app_path" "$app_cmd" "$window_class" "warm")
        if [[ "$time" != "timeout" ]]; then
            warm_times+=("$time")
            echo "  Run $i: ${time}ms"
        else
            echo "  Run $i: TIMEOUT"
        fi
    done
    
    # Calculate averages
    echo ""
    echo "───────────────────────────────────────────────────────────"
    echo " RESULTS"
    echo "───────────────────────────────────────────────────────────"
    
    if [[ ${#cold_times[@]} -gt 0 ]]; then
        local cold_sum=0
        for t in "${cold_times[@]}"; do cold_sum=$((cold_sum + t)); done
        local cold_avg=$((cold_sum / ${#cold_times[@]}))
        echo -e " Cold start average:  ${RED}${cold_avg}ms${NC}"
    fi
    
    if [[ ${#warm_times[@]} -gt 0 ]]; then
        local warm_sum=0
        for t in "${warm_times[@]}"; do warm_sum=$((warm_sum + t)); done
        local warm_avg=$((warm_sum / ${#warm_times[@]}))
        echo -e " Warm start average:  ${GREEN}${warm_avg}ms${NC}"
        
        if [[ ${#cold_times[@]} -gt 0 ]]; then
            local improvement=$(( (cold_avg - warm_avg) * 100 / cold_avg ))
            echo -e " Improvement:         ${YELLOW}${improvement}%${NC}"
        fi
    fi
    
    echo "───────────────────────────────────────────────────────────"
}

# Show preheat stats
show_stats() {
    echo ""
    log "Current Preheat Stats:"
    preheat-ctl stats 2>/dev/null || warn "Could not get stats"
}

# Quick benchmark (no cache dropping)
quick_benchmark() {
    local app_name="$1"
    local app_info="${APPS[$app_name]}"
    
    if [[ -z "$app_info" ]]; then
        error "Unknown app: $app_name"
        return 1
    fi
    
    IFS=':' read -r app_path app_cmd window_class <<< "$app_info"
    
    echo "Quick benchmark: $app_name"
    
    kill_app "$app_cmd"
    
    local start_time=$(date +%s%3N)
    $app_cmd &>/dev/null &
    local elapsed=$(wait_for_window "$window_class")
    
    if [[ "$elapsed" != "-1" ]]; then
        echo -e "Startup time: ${GREEN}${elapsed}ms${NC}"
    else
        echo -e "Startup time: ${RED}TIMEOUT${NC}"
    fi
    
    kill_app "$app_cmd"
}

# Main
main() {
    check_root
    check_deps
    
    echo ""
    echo "╔═══════════════════════════════════════════════════════════╗"
    echo "║         PREHEAT DAEMON PERFORMANCE BENCHMARK              ║"
    echo "╚═══════════════════════════════════════════════════════════╝"
    
    if [[ "$1" == "-q" || "$1" == "--quick" ]]; then
        shift
        quick_benchmark "${1:-firefox}"
    elif [[ -n "$1" ]]; then
        benchmark_app "$1"
    else
        # Benchmark all apps
        for app in calculator firefox; do
            benchmark_app "$app"
        done
    fi
    
    show_stats
    
    echo ""
    success "Benchmark complete!"
}

main "$@"
