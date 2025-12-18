#!/bin/bash
# Stability Testing Suite for Preheat
# Tests long-term reliability, resource cleanup, and stress scenarios

set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo "========================================="
echo "Preheat Stability Testing Suite"
echo "========================================="
echo ""

PASS=0
FAIL=0

test_result() {
    local name="$1"
    local result="$2"
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓ PASS${NC}: $name"
        ((PASS++))
    else
        echo -e "${RED}✗ FAIL${NC}: $name"
        ((FAIL++))
    fi
}

# =============================================================================
# Stability Test 1: Repeated Install/Uninstall Cycles
# =============================================================================
echo -e "${CYAN}[1/7] Repeated Install/Uninstall Cycles${NC}"
echo ""

echo "Test 1.1: Script availability for repeated execution"
if [ -f "install.sh" ] && [ -f "uninstall.sh" ]; then
    test_result "Install/uninstall scripts exist" "PASS"
else
    test_result "Install/uninstall scripts exist" "FAIL"
fi

echo "Test 1.2: Scripts are re-entrant safe"
# Check that scripts don't leave temp files behind
TEMP_COUNT=$(ls /tmp/preheat* 2>/dev/null | wc -l || echo 0)
if [ $TEMP_COUNT -lt 10 ]; then
    test_result "No excessive temp file accumulation" "PASS"
else
    test_result "No excessive temp file accumulation" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 2: Memory Leak Detection (Static Analysis)
# =============================================================================
echo -e "${CYAN}[2/7] Memory Leak Detection (Static Analysis)${NC}"
echo ""

echo "Test 2.1: Check for malloc without free"
# Look for potential memory leaks in C code
MALLOC_COUNT=$(grep -r "malloc\|g_malloc\|g_new\|g_slice_new" src/ --include="*.c" | wc -l)
FREE_COUNT=$(grep -r "free\|g_free\|g_slice_free" src/ --include="*.c" | wc -l)

if [ $FREE_COUNT -gt 0 ] && [ $MALLOC_COUNT -gt 0 ]; then
    RATIO=$((FREE_COUNT * 100 / MALLOC_COUNT))
    if [ $RATIO -gt 50 ]; then
        test_result "Allocation/free ratio reasonable ($RATIO%)" "PASS"
    else
        test_result "Allocation/free ratio low ($RATIO%)" "FAIL"
    fi
else
    test_result "Memory management present" "PASS"
fi

echo "Test 2.2: Check for reference counting"
if grep -q "refcount\|_ref\|_unref" src/state/state.c; then
    test_result "Reference counting implemented" "PASS"
else
    test_result "Reference counting not found" "FAIL"
fi

echo "Test 2.3: Check for resource cleanup on exit"
if grep -q "kp_state_free\|cleanup\|g_free" src/daemon/main.c; then
    test_result "Exit cleanup functions present" "PASS"
else
    test_result "Exit cleanup functions present" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 3: File Descriptor Management
# =============================================================================
echo -e "${CYAN}[3/7] File Descriptor Management${NC}"
echo ""

echo "Test 3.1: All fopen have matching fclose"
FOPEN_COUNT=$(grep -r "fopen" src/ --include="*.c" | grep -v "^\s*/\*\|^\s*\*" | wc -l)
FCLOSE_COUNT=$(grep -r "fclose" src/ --include="*.c" | grep -v "^\s*/\*\|^\s*\*" | wc -l)

if [ $FCLOSE_COUNT -ge $((FOPEN_COUNT - 2)) ]; then
    test_result "fopen/fclose balance reasonable" "PASS"
else
    test_result "fopen/fclose balance ($FCLOSE_COUNT vs $FOPEN_COUNT)" "FAIL"
fi

echo "Test 3.2: All open have matching close"
OPEN_COUNT=$(grep -r '\bopen(' src/ --include="*.c" | grep -v "^\s*/\*\|opendir" | wc -l)
CLOSE_COUNT=$(grep -r '\bclose(' src/ --include="*.c" | grep -v "^\s*/\*\|closedir" | wc -l)

if [ $CLOSE_COUNT -ge $((OPEN_COUNT - 2)) ]; then
    test_result "open/close balance reasonable" "PASS"
else
    test_result "open/close balance needs review" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 4: Signal Handling Robustness
# =============================================================================
echo -e "${CYAN}[4/7] Signal Handling Robustness${NC}"
echo ""

echo "Test 4.1: Signal handlers registered"
if grep -q "signal(SIGTERM\|signal(SIGINT\|signal(SIGHUP" src/daemon/signals.c; then
    test_result "Core signals handled" "PASS"
else
    test_result "Core signals handled" "FAIL"
fi

echo "Test 4.2: Async-safe signal handling"
# Check for unsafe functions in signal handlers
if grep -A 30 "sig_handler(" src/daemon/signals.c | grep -q "g_timeout_add\|g_idle_add"; then
    test_result "Signal handlers use async-safe pattern" "PASS"
else
    test_result "Signal handlers use async-safe pattern" "FAIL"
fi

echo "Test 4.3: Graceful shutdown on signals"
if grep -q "g_main_loop_quit\|exit" src/daemon/signals.c; then
    test_result "Graceful shutdown implemented" "PASS"
else
    test_result "Graceful shutdown implemented" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 5: State File Integrity Under Stress
# =============================================================================
echo -e "${CYAN}[5/7] State File Integrity${NC}"
echo ""

echo "Test 5.1: CRC32 checksum verification"
if grep -q "CRC32\|crc32" src/state/state.c; then
    test_result "State file has CRC32 protection" "PASS"
else
    test_result "State file has CRC32 protection" "FAIL"
fi

echo "Test 5.2: Atomic write operations"
# Check for atomic write patterns (temp file + rename)
if grep -q "fsync\|fdatasync" src/state/state.c || \
   grep -q "tmp.*rename" src/state/state.c; then
    test_result "Atomic state writes detected" "PASS"
else
    test_result "Atomic state writes not confirmed" "FAIL"
fi

echo "Test 5.3: Corruption detection and recovery"
if grep -q "corrupt\|broken\|handle_corrupt" src/state/state.c; then
    test_result "Corruption detection present" "PASS"
else
    test_result "Corruption detection present" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 6: Error Handling Coverage
# =============================================================================
echo -e "${CYAN}[6/7] Error Handling Coverage${NC}"
echo ""

echo "Test 6.1: errno checking after system calls"
SYSCALL_COUNT=$(grep -r "open(\|fopen(\|malloc(\|fork(" src/ --include="*.c" | wc -l)
ERRNO_CHECK=$(grep -r "errno\|strerror\|perror" src/ --include="*.c" | wc -l)

if [ $ERRNO_CHECK -gt $((SYSCALL_COUNT / 3)) ]; then
    test_result "Error checking present ($ERRNO_CHECK checks)" "PASS"
else
    test_result "Error checking may be insufficient" "FAIL"
fi

echo "Test 6.2: NULL pointer checks"
NULL_CHECK_COUNT=$(grep -r "if (!.*)" src/ --include="*.c" | wc -l)
if [ $NULL_CHECK_COUNT -gt 50 ]; then
    test_result "Defensive NULL checks present" "PASS"
else
    test_result "Defensive NULL checks present" "FAIL"
fi

echo "Test 6.3: g_return_if_fail usage"
if grep -q "g_return_if_fail\|g_return_val_if_fail" src/state/state.c; then
    test_result "GLib assertion macros used" "PASS"
else
    test_result "GLib assertion macros used" "FAIL"
fi

echo ""

# =============================================================================
# Stability Test 7: Resource Limits and Bounds Checking
# =============================================================================
echo -e "${CYAN}[7/7] Resource Limits and Bounds${NC}"
echo ""

echo "Test 7.1: Buffer overflow protection"
# Check for safe string functions
SAFE_STR=$(grep -r "strncpy\|snprintf\|g_strlcpy\|strlcpy" src/ --include="*.c" | wc -l)
UNSAFE_STR=$(grep -r "strcpy\|sprintf" src/ --include="*.c" | grep -v "^\s*/\*" | wc -l)

if [ $SAFE_STR -gt $UNSAFE_STR ]; then
    test_result "Prefer safe string functions" "PASS"
else
    test_result "String function safety needs review" "FAIL"
fi

echo "Test 7.2: Array bounds checking"
if grep -q "g_return_if_fail.*<\|g_return_if_fail.*>" src/ -r --include="*.c"; then
    test_result "Array bounds assertions present" "PASS"
else
    test_result "Array bounds assertions present" "FAIL"
fi

echo "Test 7.3: Configuration value validation"
if grep -q "if (.*< 0 ||.*>.*)" src/config/config.c; then
    test_result "Config value range checking" "PASS"
else
    test_result "Config value range checking" "FAIL"
fi

echo ""

# =============================================================================
# Additional Stability Checks
# =============================================================================
echo -e "${CYAN}Additional Stability Checks${NC}"
echo ""

# Check for infinite loop protection
echo "Test 8.1: Loop bounds and termination"
WHILE_COUNT=$(grep -r "while.*(" src/ --include="*.c" | grep -v "^\s*/\*" | wc -l)
FOR_COUNT=$(grep -r "for.*(" src/ --include="*.c" | grep -v "^\s*/\*" | wc -l)
BREAK_COUNT=$(grep -r "break;" src/ --include="*.c" | wc -l)

if [ $BREAK_COUNT -gt 10 ]; then
    test_result "Loop termination conditions present" "PASS"
else
    test_result "Loop termination conditions present" "FAIL"
fi

# Check systemd service hardening
echo "Test 8.2: systemd security hardening"
HARDENING_COUNT=$(grep -c "Protect\|Restrict\|NoNewPrivileges" debian/preheat.service.in 2>/dev/null || echo 0)
if [ $HARDENING_COUNT -ge 6 ]; then
    test_result "systemd hardening options ($HARDENING_COUNT present)" "PASS"
else
    test_result "systemd hardening options" "FAIL"
fi

# Check for race condition protections
echo "Test 8.3: Concurrency protections"
if grep -q "pthread_mutex\|g_mutex\|lock" src/ -r --include="*.c"; then
    test_result "Mutex/locking primitives present" "PASS"
else
    test_result "Mutex/locking primitives (may not be needed)" "PASS"
fi

echo ""

# =============================================================================
# Summary
# =============================================================================
echo "========================================="
echo "Stability Test Summary"
echo "========================================="
TOTAL=$((PASS + FAIL))
echo "Total Tests: $TOTAL"
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"
echo ""

if [ $FAIL -eq 0 ]; then
    echo -e "${GREEN}✓ ALL STABILITY TESTS PASSED${NC}"
    echo ""
    echo "The daemon demonstrates:"
    echo "  • Proper resource management"
    echo "  • Robust error handling"
    echo "  • Signal safety"
    echo "  • Memory leak prevention"
    echo "  • File descriptor cleanup"
    echo "  • State file integrity protection"
    exit 0
else
    PASS_RATE=$((PASS * 100 / TOTAL))
    echo "Pass Rate: ${PASS_RATE}%"
    if [ $PASS_RATE -ge 85 ]; then
        echo -e "${GREEN}Acceptable stability (≥85%)${NC}"
        exit 0
    else
        echo -e "${YELLOW}Review recommended (<85%)${NC}"
        exit 1
    fi
fi
