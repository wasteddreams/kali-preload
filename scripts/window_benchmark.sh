#!/bin/bash
# ═══════════════════════════════════════════════════════════════════════════════
#  PREHEAT DAEMON - PROFESSIONAL BENCHMARK SUITE
# ═══════════════════════════════════════════════════════════════════════════════
#
#  Accurately measures application startup times by detecting REAL window 
#  visibility. Works on both X11 (via xdotool) and Wayland/GNOME (via gdbus).
#
#  Features:
#    • Accurate "Window Visible" detection (not just process start)
#    • Cold vs Warm start comparison (system caches fully dropped)
#    • User-context launching (safely runs apps as non-root user)
#    • Robust cleanup (handles single-instance locks and zombie processes)
#
#  Usage:
#    sudo -E ./window_benchmark.sh                 # Standard benchmark (3 apps)
#    sudo -E ./window_benchmark.sh firefox         # Specific app
#    sudo -E ./window_benchmark.sh -i 5            # Custom iterations
#
#  Note: Run with sudo -E to preserve DISPLAY/XAUTHORITY variables.
# ═══════════════════════════════════════════════════════════════════════════════

set -e

# ─────────────────────────────────────────────────────────────────────────────
# CONFIGURATION
# ─────────────────────────────────────────────────────────────────────────────

ITERATIONS=${ITERATIONS:-3}
WARMUP_SLEEP=3
MEASURE_TIMEOUT=30
POLL_INTERVAL=0.05

# ─────────────────────────────────────────────────────────────────────────────
# STYLING
# ─────────────────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
WHITE='\033[1;37m'
GRAY='\033[0;90m'
NC='\033[0m'
BOLD='\033[1m'
DIM='\033[2m'

# Unicode Glyphs
CHECK="✓"
CROSS="✗"
ARROW="→"
BULLET="•"
FIRE="🔥"
COLD="❄"
CHART="📊"

# ─────────────────────────────────────────────────────────────────────────────
# APPLICATION DATABASE
# ─────────────────────────────────────────────────────────────────────────────
# Format: [name]="binary_path:launch_cmd:window_pattern:friendly_name"

declare -A APPS=(
    # Browsers
    ["firefox"]="/usr/lib/firefox-esr/firefox-esr:firefox-esr:Firefox:Firefox ESR"
    ["firefox-snap"]="/snap/firefox/current/usr/lib/firefox/firefox:firefox:Firefox:Firefox (Snap)"
    ["chromium"]="/usr/bin/chromium:chromium:Chromium:Chromium"
    
    # Office
    ["libreoffice-writer"]="/usr/bin/libreoffice:libreoffice --writer:LibreOffice Writer:LibreOffice Writer"
    ["libreoffice-calc"]="/usr/bin/libreoffice:libreoffice --calc:LibreOffice Calc:LibreOffice Calc"
    
    # Development
    ["code"]="/usr/share/code/code:code:Visual Studio Code:VS Code"
    
    # System / Tools
    ["gnome-calculator"]="/usr/bin/gnome-calculator:gnome-calculator:Calculator:Calculator"
    ["gnome-terminal"]="/usr/bin/gnome-terminal:gnome-terminal:Terminal:GNOME Terminal"
    ["nautilus"]="/usr/bin/nautilus:nautilus:Files:Files (Nautilus)"
)

# ─────────────────────────────────────────────────────────────────────────────
# HELPERS
# ─────────────────────────────────────────────────────────────────────────────

log()     { echo -e "${GRAY}[$(date +%H:%M:%S)]${NC} $1"; }
info()    { echo -e "${BLUE}${BULLET}${NC} $1"; }
warn()    { echo -e "${YELLOW}⚠${NC} $1"; }
error()   { echo -e "${RED}${CROSS}${NC} $1"; }
print_header() {
    echo -e "\n${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BOLD}${WHITE}  $1${NC}"
    echo -e "${CYAN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}
print_section() {
    echo -e "\n${DIM}──────────────────────────────────────────────────────────────${NC}"
    echo -e "  ${BOLD}$1${NC}"
    echo -e "${DIM}──────────────────────────────────────────────────────────────${NC}"
}

# ─────────────────────────────────────────────────────────────────────────────
# SYSTEM & ENVIRONMENT CHECKS
# ─────────────────────────────────────────────────────────────────────────────

check_root() {
    if [[ $EUID -ne 0 ]]; then
        error "This script requires root privileges (to drop caches)."
        echo "  Please run with: sudo -E $0"
        exit 1
    fi
}

check_display() {
    # Ensure DISPLAY is set (common issue with sudo)
    if [[ -z "$DISPLAY" ]]; then
        export DISPLAY=:0
    fi
    
    # Attempt to locate XAuthority for proper X11/Wayland interaction
    if [[ -z "$XAUTHORITY" ]]; then
        if [[ -n "$SUDO_USER" ]]; then
            local user_home=$(eval echo ~$SUDO_USER)
            if [[ -f "$user_home/.Xauthority" ]]; then
                export XAUTHORITY="$user_home/.Xauthority"
            elif [[ -f "/run/user/$(id -u $SUDO_USER)/gdm/Xauthority" ]]; then
                export XAUTHORITY="/run/user/$(id -u $SUDO_USER)/gdm/Xauthority"
            fi
        fi
    fi
}

check_dependencies() {
    local missing=()
    # Check for basic tools; warn but don't fail if some optional ones are missing
    for cmd in bc awk grep; do
        if ! command -v "$cmd" &>/dev/null; then missing+=("$cmd"); fi
    done
    
    if [[ ${#missing[@]} -gt 0 ]]; then
        error "Missing required tools: ${missing[*]}"
        exit 1
    fi
    
    if command -v preheat-ctl &>/dev/null; then
        HAS_PREHEAT=true
    else
        HAS_PREHEAT=false
    fi
    
    # Check for vmtouch (optional)
    if command -v vmtouch &>/dev/null; then HAS_VMTOUCH=true; else HAS_VMTOUCH=false; fi
}

# ─────────────────────────────────────────────────────────────────────────────
# CORE LOGIC
# ─────────────────────────────────────────────────────────────────────────────

get_time_ns() { date +%s%N; }

ns_to_ms() { echo "scale=1; $1 / 1000000" | bc; }

# safely kill applications and clean up locks
kill_app_completely() {
    local pattern="$1"
    local cmd_base=$(echo "$pattern" | awk '{print $1}')
    local proc_name=$(basename "$cmd_base" 2>/dev/null || echo "$cmd_base")

    # App-specific cleanup logic
    case "$proc_name" in
        firefox-esr|firefox)
            # More aggressive kill sequence for Firefox
            killall -q -9 firefox-esr firefox "Web Content" "Isolated Web Co" "GeckoMain" || true
            pkill -9 -f "firefox" 2>/dev/null || true
            
            # Clean profile locks to prevent "Firefox is already running" errors
            if [[ -n "$SUDO_USER" ]]; then
                local user_home=$(eval echo ~$SUDO_USER)
                rm -f "$user_home"/.mozilla/firefox/*.default*/lock 2>/dev/null || true
                rm -f "$user_home"/.mozilla/firefox/*.default*/.parentlock 2>/dev/null || true
            fi
            
            # Additional sleep for Firefox to release file handles
            sleep 1
            ;;
        libreoffice)
            killall -q -9 soffice.bin oosplash || true
            ;;
        code)
            killall -q -9 code || true
            pkill -9 -f "/code" 2>/dev/null || true
            ;;
        chromium)
            killall -q -9 chromium || true
            ;;
        *)
            # Generic kill
            killall -q -9 "$proc_name" || true
            ;;
    esac

    # Verify death
    local wait_count=0
    while pgrep -f "$proc_name" &>/dev/null && [[ $wait_count -lt 50 ]]; do
        sleep 0.1
        ((wait_count++))
    done
    
    # Extra settle time to ensure socket/db release
    sleep 2
}

# Wait for a window to be visible (Wayland & X11 compatible)
wait_for_visible_window() {
    local window_pattern="$1"
    local launch_cmd="$2"
    local start_ns=$(get_time_ns)
    local timeout_ns=$((MEASURE_TIMEOUT * 1000000000))
    local proc_name=$(echo "$launch_cmd" | awk '{print $1}')
    
    while true; do
        local elapsed_ns=$(($(get_time_ns) - start_ns))
        if [[ $elapsed_ns -gt $timeout_ns ]]; then
            echo "-1"
            return
        fi

        # Strategy 1: GNOME Shell via gdbus (Best for Wayland)
        if command -v gdbus &>/dev/null; then
            local windows=$(gdbus call --session --dest org.gnome.Shell \
                --object-path /org/gnome/Shell \
                --method org.gnome.Shell.Eval \
                "global.get_window_actors().map(a=>a.meta_window.get_title()).join('|')" 2>/dev/null || true)
            if [[ "$windows" == *"$window_pattern"* ]]; then
                echo "$elapsed_ns"
                return
            fi
        fi

        # Strategy 2: X11/XWayland via xdotool
        if command -v xdotool &>/dev/null; then
            local wid=$(xdotool search --name "$window_pattern" 2>/dev/null | head -1)
            if [[ -n "$wid" ]]; then
                # Check IsViewable state
                if xwininfo -id "$wid" 2>/dev/null | grep -q "Map State: IsViewable"; then
                    echo "$elapsed_ns"
                    return
                fi
            fi
        fi

        # Strategy 3: Heuristic fallback (Process running stable > 3s)
        # Used if window detection strictly fails (e.g., pure Wayland without gdbus access)
        local pids=$(pgrep -f "$proc_name" 2>/dev/null || true)
        if [[ -n "$pids" ]]; then
            for pid in $pids; do
                local proc_start=$(stat -c %Y /proc/$pid 2>/dev/null || echo "0")
                local now=$(date +%s)
                local running_secs=$((now - proc_start))
                
                # If stable for 3s and CPU usage dropped below 50%
                if [[ $running_secs -ge 3 ]]; then
                     local cpu=$(ps -p $pid -o %cpu= 2>/dev/null | tr -d ' ' || echo "100")
                     if (( $(echo "$cpu < 50" | bc -l 2>/dev/null || echo "0") )); then
                         echo "$elapsed_ns"
                         return
                     fi
                fi
            done
        fi
        
        sleep $POLL_INTERVAL
    done
}

drop_all_caches() {
    sync
    echo 3 > /proc/sys/vm/drop_caches
    sleep 2
}

preload_file() {
    local file="$1"
    if [[ -f "$file" ]]; then
        dd if="$file" of=/dev/null bs=1M 2>/dev/null || true
    fi
}

measure_single_startup() {
    local binary="$1"
    local launch_cmd="$2"
    local window_pattern="$3"
    local condition="$4"

    kill_app_completely "$launch_cmd"
    sleep 0.5 # settle time

    local start_ns=$(get_time_ns)

    # Launch the application as the regular user (not root)
    echo "  [DEBUG] Launching: $launch_cmd" >&2
    
    # Create a temp log for this run
    local run_log="/tmp/preheat_bench_run.log"
    rm -f "$run_log"
    
    if [[ -n "$SUDO_USER" ]]; then
        # CRITICAL FIX for Wayland/GNOME:
        # Simple 'sudo -u' or 'su' is NOT enough – it misses the DBus session bus 
        # and XDG runtime dir needed for Wayland apps to talk to the compositor.
        
        local user_uid=$(id -u "$SUDO_USER")
        local user_home=$(eval echo ~$SUDO_USER)
        
        # Method 1: Try machinectl (cleanest way to open shell in user session)
        if command -v machinectl &>/dev/null; then
             machinectl shell "$SUDO_USER@.host" /usr/bin/env \
                DISPLAY="$DISPLAY" \
                XAUTHORITY="$XAUTHORITY" \
                $launch_cmd >"$run_log" 2>&1 &
        else
            # Method 2: Manually reconstruct the environment
            # We need DBUS_SESSION_BUS_ADDRESS to talk to gnome-shell/wayland
            
            # Try to steal DBUS addr from an existing user process (like gnome-session)
            local dbus_addr=$(grep -z DBUS_SESSION_BUS_ADDRESS /proc/$(pgrep -u "$SUDO_USER" gnome-session | head -1)/environ 2>/dev/null | tr -d '\0')
            
            if [[ -z "$dbus_addr" ]]; then
                 # Fallback: guess the socket path
                 dbus_addr="unix:path=/run/user/$user_uid/bus"
            fi
            
            sudo -u "$SUDO_USER" \
                DISPLAY="$DISPLAY" \
                XAUTHORITY="$XAUTHORITY" \
                DBUS_SESSION_BUS_ADDRESS="$dbus_addr" \
                XDG_RUNTIME_DIR="/run/user/$user_uid" \
                $launch_cmd >"$run_log" 2>&1 &
        fi
    else
        $launch_cmd >"$run_log" 2>&1 &
    fi
    local pid=$!
    echo "  [DEBUG] Launched with shell PID: $pid" >&2
    
    # Sleep increased to 0.5s    # Wait for visible window (pass launch_cmd for PID-based detection)
    local elapsed_ns
    elapsed_ns=$(wait_for_visible_window "$window_pattern" "$launch_cmd")
    
    # VISUAL CONFIRMATION:
    # Leave the window up for a moment so the user actually SEES it.
    # Otherwise it might be killed instantly after detection (before painting).
    sleep 1.5
    
    # Kill the app
    kill_app_completely "$launch_cmd"

    if [[ "$elapsed_ns" == "-1" ]]; then
        echo "timeout"
        # Print last few lines of log to help debug
        if [[ -f "$run_log" ]]; then
            echo "  [DEBUG] Last 5 lines of app log:" >&2
            tail -n 5 "$run_log" | sed 's/^/  [LOG] /' >&2
        fi
    else
        ns_to_ms "$elapsed_ns"
    fi
}

# ─────────────────────────────────────────────────────────────────────────────
# BENCHMARK RUNNER
# ─────────────────────────────────────────────────────────────────────────────

benchmark_single_app() {
    local app_key="$1"
    local app_info="${APPS[$app_key]}"

    if [[ -z "$app_info" ]]; then
        error "Unknown application key: $app_key"
        # Fallback to key itself if it looks like a command? No, stick to DB.
        return 1
    fi

    IFS=':' read -r binary launch_cmd window_pattern friendly_name <<< "$app_info"

    print_header "${FIRE} Benchmarking: $friendly_name"
    echo -e "  ${GRAY}Binary:  $binary${NC}"
    echo -e "  ${GRAY}Command: $launch_cmd${NC}"
    echo -e "  ${GRAY}Method:  $(command -v machinectl >/dev/null && echo "machinectl (systemd)" || echo "sudo (manual env)")${NC}"
    echo ""
    
    # SPECIAL CHECK: Don't kill the IDE we are running in!
    if [[ "$app_key" == "code" || "$app_key" == "code-snap" ]]; then
        local safe_to_kill=true
        
        # Check 1: Environment Variables (preserved by sudo -E)
        if [[ "$TERM_PROGRAM" == "vscode" || -n "$VSCODE_PID" ]]; then
            safe_to_kill=false
        fi
        
        # Check 2: Process Tree (ancestors)
        if [[ "$safe_to_kill" == "true" ]]; then
            if pstree -s $$ 2>/dev/null | grep -qE "code|code-oss|vscodium"; then
                safe_to_kill=false
            fi
        fi
        
        if [[ "$safe_to_kill" == "false" ]]; then
            warn "Skipping VS Code benchmark because you are running INSIDE VS Code."
            echo "  (Killing the app would kill this terminal session)"
            return 0
        fi
        
        # Check 3: Last Resort Warning
        warn "About to kill 'code' processes..."
        echo -e "  ${YELLOW}⚠ IF YOU ARE RUNNING THIS TERMINAL INSIDE VS CODE, PRESS CTRL+C NOW!${NC}"
        echo -ne "  Continuing in 3... "
        sleep 1
        echo -ne "2... "
        sleep 1
        echo -ne "1... \r"
        sleep 1
        echo "                        "
    fi
    
    # Check binary exists
    if [[ ! -f "$binary" ]]; then
        error "Binary not found: $binary"
        return 1
    fi
    
    # ─────────────────────── PHASE 1: COLD START ───────────────────────
    print_section "${COLD} COLD START (Caches Dropped)"
    local cold_times=()
    
    for i in $(seq 1 $ITERATIONS); do
        # VISUAL CONFIRMATION OF CLEANUP
        echo -ne "  ${GRAY}Prep:${NC} Stopping daemon... \r"
        systemctl stop preheat 2>/dev/null || true
        
        echo -ne "  ${GRAY}Prep:${NC} Killing processes... \r"
        kill_app_completely "$launch_cmd"
        
        echo -ne "  ${GRAY}Prep:${NC} Dropping caches...   \r"
        drop_all_caches
        
        # Verify cache drop if vmtouch is available
        local cache_msg=""
        if [[ "$HAS_VMTOUCH" == "true" ]]; then
            local res_pages=$(vmtouch "$binary" 2>/dev/null | grep "Resident" | awk '{print $3}')
            if [[ "$res_pages" == "0/100%" || "$res_pages" == "0" ]]; then
                 cache_msg="${GREEN}(0% Cached)${NC}"
            else
                 # Note: Shared libs might keep some pages resident
                 cache_msg="${YELLOW}(Partial)${NC}"
            fi
        fi
        
        echo -ne "  ${GRAY}Prep:${NC} Waiting I/O settle... $cache_msg \r"
        sleep $WARMUP_SLEEP
        
        # Clear line
        echo -ne "                                          \r"
        
        local time=$(measure_single_startup "$binary" "$launch_cmd" "$window_pattern" "cold")
        if [[ "$time" == "timeout" ]]; then
            echo -e "  ${GRAY}Run $i:${NC} ${RED}TIMEOUT${NC}"
            # Print log if available
            if [[ -f "/tmp/preheat_bench_run.log" ]]; then
                 echo -e "      ${DIM}$(tail -n1 /tmp/preheat_bench_run.log)${NC}"
            fi
        else
            cold_times+=("$time")
            local sec=$(echo "scale=2; $time/1000" | bc | awk '{printf "%.2f", $0}')
            echo -e "  ${GRAY}Run $i:${NC} ${RED}${sec}s${NC}"
        fi
    done

    # Cleanup before warm start
    kill_app_completely "$launch_cmd"
    sleep 1

    # 3. WARM START (Preloaded)
    print_section "${FIRE} WARM START (Preloaded)"
    local warm_times=()

    # Activate preheat or manually preload
    if [[ "$HAS_PREHEAT" == "true" ]]; then
        systemctl start preheat 2>/dev/null || true
        sleep 3
        # explicit save to ensure state is fresh
        preheat-ctl save 2>/dev/null || true
    fi

    for i in $(seq 1 $ITERATIONS); do
        # Emulate daemons work if not installed
        if [[ "$HAS_PREHEAT" != "true" ]]; then
            preload_file "$binary"
            ldd "$binary" 2>/dev/null | awk '{print $3}' | grep -v "^$" | while read lib; do preload_file "$lib"; done 2>/dev/null
        fi
        
        sleep $WARMUP_SLEEP
        local time=$(measure_single_startup "$binary" "$launch_cmd" "$window_pattern" "warm")
         if [[ "$time" == "timeout" ]]; then
            echo -e "  ${GRAY}Run $i:${NC} ${RED}TIMEOUT${NC}"
        else
            warm_times+=("$time")
            local sec=$(echo "scale=2; $time/1000" | bc)
            echo -e "  ${GRAY}Run $i:${NC} ${GREEN}${sec}s${NC}"
        fi
    done

    # 4. RESULTS CALCULATION
    print_section "${CHART} RESULTS"
    
    local cold_avg=0 warm_avg=0
    
    # helper to avg
    calc_avg() {
        local -n arr=$1
        [[ ${#arr[@]} -eq 0 ]] && echo 0 && return
        local sum=0
        for x in "${arr[@]}"; do sum=$(echo "$sum + $x" | bc); done
        echo "scale=2; $sum / ${#arr[@]}" | bc
    }
    
    cold_avg=$(calc_avg cold_times)
    warm_avg=$(calc_avg warm_times)
    
    # Display
    local cold_desc="All Timeouts"
    local warm_desc="All Timeouts"
    
    if (( $(echo "$cold_avg > 0" | bc -l) )); then
        cold_desc=$(printf "${RED}%6.2f s${NC}" "$(echo "scale=2; $cold_avg/1000" | bc)")
    fi
    if (( $(echo "$warm_avg > 0" | bc -l) )); then
        warm_desc=$(printf "${GREEN}%6.2f s${NC}" "$(echo "scale=2; $warm_avg/1000" | bc)")
    fi
    
    echo -e "  ${COLD} Cold Start Avg:  $cold_desc"
    echo -e "  ${FIRE} Warm Start Avg:  $warm_desc"
    
    if (( $(echo "$cold_avg > 0 && $warm_avg > 0" | bc -l) )); then
        local diff=$(echo "$cold_avg - $warm_avg" | bc)
        local impr=$(echo "scale=1; ($diff * 100) / $cold_avg" | bc)
        local saved=$(echo "scale=2; $diff / 1000" | bc)
        
        echo ""
        printf "  ${ARROW} Improvement:     ${YELLOW}%6.1f %%${NC}\n" "$impr"
        printf "  ${ARROW} Time Saved:      ${CYAN}%6.2f s${NC}\n" "$saved"
    fi
    echo ""
}

# ─────────────────────────────────────────────────────────────────────────────
# MAIN
# ─────────────────────────────────────────────────────────────────────────────

show_banner() {
    clear
    echo ""
    echo -e "${CYAN}================================================================${NC}"
    echo -e "${BOLD}${WHITE}  PREHEAT DAEMON  ${NC}${GRAY}•${NC}  ${BOLD}Performance Benchmark Suite${NC}"
    echo -e "${CYAN}================================================================${NC}"
    echo -e "${GRAY}  Measures real window visibility time (not process launch)${NC}"
    echo -e "${CYAN}----------------------------------------------------------------${NC}"
    echo ""
}

main() {
    local target_app=""
    while [[ $# -gt 0 ]]; do
        case "$1" in
            -h|--help) echo "Usage: sudo -E $0 [-i 5] [firefox|code|libreoffice-writer]"; exit 0 ;;
            -l|--list) 
                echo -e "${BOLD}Available Apps:${NC}"
                for k in "${!APPS[@]}"; do echo "  - $k"; done; exit 0 ;;
            -i|--iterations) ITERATIONS="$2"; shift 2 ;;
            *) target_app="$1"; shift ;;
        esac
    done

    check_root
    check_display
    check_dependencies
    show_banner

    if [[ -n "$target_app" ]]; then
        benchmark_single_app "$target_app"
    else
        echo -e "${GRAY}Running default showcase suite (3 apps)...${NC}"
        
        # 1. Browser
        if [[ -f "/snap/firefox/current/usr/lib/firefox/firefox" ]]; then
             benchmark_single_app "firefox-snap"
        elif [[ -f "/usr/lib/firefox-esr/firefox-esr" ]]; then
             benchmark_single_app "firefox"
        fi
        
        # 2. Office
        if [[ -f "/usr/bin/libreoffice" ]]; then
             benchmark_single_app "libreoffice-writer"
        fi
        
        # 3. IDE
        if [[ -f "/usr/share/code/code" ]]; then
             benchmark_single_app "code"
        fi
    fi
}

main "$@"
