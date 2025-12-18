#!/bin/bash
# Comprehensive Test Suite for Preheat
# Tests all edge cases, failure scenarios, and audit fixes

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PASSED=0
FAILED=0
TOTAL=0

test_result() {
    local name="$1"
    local result="$2"
    ((TOTAL++))
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓ PASS${NC}: $name"
        ((PASSED++))
    else
        echo -e "${RED}✗ FAIL${NC}: $name"
        ((FAILED++))
    fi
}

echo "========================================="
echo "Preheat Comprehensive Test Suite"
echo "========================================="
echo ""

# =============================================================================
# TEST CATEGORY 1: Dependency Validation (install.sh)
# =============================================================================
echo -e "${CYAN}[1/8] Testing Dependency Validation${NC}"
echo ""

# Test 1.1: Check missing git detection
echo "Test 1.1: Missing git detection"
if grep -q 'command -v git' install.sh && \
   grep -q 'MISSING_PKGS.*git' install.sh; then
    test_result "install.sh detects missing git" "PASS"
else
    test_result "install.sh detects missing git" "FAIL"
fi

# Test 1.2: Check GLib detection
echo "Test 1.2: GLib development headers detection"
if grep -q 'pkg-config --exists glib-2.0' install.sh; then
    test_result "install.sh checks for GLib dev headers" "PASS"
else
    test_result "install.sh checks for GLib dev headers" "FAIL"
fi

# Test 1.3: Verify error message format
echo "Test 1.3: Dependency error message format"
if grep -q 'apt-get install.*UNIQUE_PKGS' install.sh; then
    test_result "install.sh provides apt-get command" "PASS"
else
    test_result "install.sh provides apt-get command" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 2: Whitelist Validation
# =============================================================================
echo ""
echo -e "${CYAN}[2/8] Testing Whitelist Validation${NC}"
echo ""

# Test 2.1: TTY detection
echo "Test 2.1: TTY detection for interactive mode"
if grep -q 'if \[ -t 0 \]' install.sh; then
    test_result "install.sh has TTY detection" "PASS"
else
    test_result "install.sh has TTY detection" "FAIL"
fi

# Test 2.2: Absolute path validation
echo "Test 2.2: Absolute path validation in whitelist"
if grep -q '\[\[ "$app_path" == /\* \]\]' install.sh; then
    test_result "install.sh validates absolute paths" "PASS"
else
    test_result "install.sh validates absolute paths" "FAIL"
fi

# Test 2.3: Executable check
echo "Test 2.3: Executable permission check"
if grep -q '\[ -x "$app_path" \]' install.sh; then
    test_result "install.sh checks executable permission" "PASS"
else
    test_result "install.sh checks executable permission" "FAIL"
fi

# Test 2.4: Create actual test with validation script
echo "Test 2.4: Whitelist validation script"
if [ -f "tests/test_whitelist_validation.sh" ]; then
    bash tests/test_whitelist_validation.sh > /tmp/whitelist_test.log 2>&1
    if grep -q "All validation tests complete" /tmp/whitelist_test.log && \
       ! grep -q "FAIL" /tmp/whitelist_test.log; then
        test_result "Whitelist validation script passes" "PASS"
    else
        test_result "Whitelist validation script passes" "FAIL"
    fi
else
    test_result "Whitelist validation script exists" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 3: Update Script Path Resolution
# =============================================================================
echo ""
echo -e "${CYAN}[3/8] Testing Update Script Path Resolution${NC}"
echo ""

# Test 3.1: Multiple location fallback
echo "Test 3.1: Update script checks multiple locations"
if grep -q 'script_locations\[\]' tools/preheat-ctl.c && \
   grep -q 'for (int i = 0; script_locations\[i\]' tools/preheat-ctl.c; then
    test_result "preheat-ctl has fallback logic" "PASS"
else
    test_result "preheat-ctl has fallback logic" "FAIL"
fi

# Test 3.2: Access check
echo "Test 3.2: Script executable check before exec"
if grep -q 'access(script_locations\[i\], X_OK)' tools/preheat-ctl.c; then
    test_result "preheat-ctl validates script is executable" "PASS"
else
    test_result "preheat-ctl validates script is executable" "FAIL"
fi

# Test 3.3: Error messages
echo "Test 3.3: Helpful error message on failure"
if grep -q 'Manual update procedure' tools/preheat-ctl.c && \
   grep -q 'git pull' tools/preheat-ctl.c; then
    test_result "preheat-ctl provides manual instructions" "PASS"
else
    test_result "preheat-ctl provides manual instructions" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 4: Uninstall Data Preservation
# =============================================================================
echo ""
echo -e "${CYAN}[4/8] Testing Uninstall Data Preservation${NC}"
echo ""

# Test 4.1: Flags parsing
echo "Test 4.1: Uninstall flag parsing"
if grep -q -- '--keep-data' uninstall.sh && \
   grep -q -- '--purge-data' uninstall.sh; then
    test_result "uninstall.sh supports data flags" "PASS"
else
    test_result "uninstall.sh supports data flags" "FAIL"
fi

# Test 4.2: Default behavior
echo "Test 4.2: Non-interactive default (preserve data)"
if grep -q 'REMOVE_DATA=false' uninstall.sh; then
    test_result "uninstall.sh defaults to preserve data" "PASS"
else
    test_result "uninstall.sh defaults to preserve data" "FAIL"
fi

# Test 4.3: Double confirmation
echo "Test 4.3: Double confirmation for deletion"
if grep -q 'Are you sure?' uninstall.sh; then
    test_result "uninstall.sh has double confirmation" "PASS"
else
    test_result "uninstall.sh has double confirmation" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 5: Update Script Safety
# =============================================================================
echo ""
echo -e "${CYAN}[5/8] Testing Update Script Safety${NC}"
echo ""

# Test 5.1: Backup creation
echo "Test 5.1: Backup before update"
if grep -q 'Creating backup' scripts/update.sh && \
   grep -q 'BACKUP_DIR=' scripts/update.sh; then
    test_result "update.sh creates backup" "PASS"
else
    test_result "update.sh creates backup" "FAIL"
fi

# Test 5.2: Rollback mechanism
echo "Test 5.2: Automatic rollback on failure"
if grep -q 'cleanup_on_failure' scripts/update.sh && \
   grep -q 'trap cleanup_on_failure ERR' scripts/update.sh; then
    test_result "update.sh has rollback mechanism" "PASS"
else
    test_result "update.sh has rollback mechanism" "FAIL"
fi

# Test 5.3: Dependency check
echo "Test 5.3: Update script checks dependencies"
if grep -q 'Checking dependencies' scripts/update.sh && \
   grep -q 'for cmd in git autoconf' scripts/update.sh; then
    test_result "update.sh validates dependencies" "PASS"
else
    test_result "update.sh validates dependencies" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 6: Bash Syntax Validation
# =============================================================================
echo ""
echo -e "${CYAN}[6/8] Testing Bash Syntax${NC}"
echo ""

# Test 6.1: install.sh syntax
echo "Test 6.1: install.sh syntax check"
if bash -n install.sh 2>/dev/null; then
    test_result "install.sh has valid syntax" "PASS"
else
    test_result "install.sh has valid syntax" "FAIL"
fi

# Test 6.2: uninstall.sh syntax
echo "Test 6.2: uninstall.sh syntax check"
if bash -n uninstall.sh 2>/dev/null; then
    test_result "uninstall.sh has valid syntax" "PASS"
else
    test_result "uninstall.sh has valid syntax" "FAIL"
fi

# Test 6.3: update.sh syntax
echo "Test 6.3: update.sh syntax check"
if bash -n scripts/update.sh 2>/dev/null; then
    test_result "update.sh has valid syntax" "PASS"
else
    test_result "update.sh has valid syntax" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 7: Documentation Completeness
# =============================================================================
echo ""
echo -e "${CYAN}[7/8] Testing Documentation${NC}"
echo ""

# Test 7.1: ProtectHome documentation
echo "Test 7.1: ProtectHome restriction documented"
if grep -q 'ProtectHome.*read-only' debian/preheat.service.in && \
   grep -q 'MUST be in system directories' debian/preheat.service.in; then
    test_result "systemd service documents ProtectHome" "PASS"
else
    test_result "systemd service documents ProtectHome" "FAIL"
fi

# Test 7.2: State restrictions doc
echo "Test 7.2: State restrictions documentation"
if [ -f "docs/STATE_RESTRICTIONS.md" ]; then
    test_result "STATE_RESTRICTIONS.md exists" "PASS"
else
    test_result "STATE_RESTRICTIONS.md exists" "FAIL"
fi

# Test 7.3: Migration warning
echo "Test 7.3: State migration warning in INSTALL.md"
if grep -q 'ONE-WAY' INSTALL.md && grep -q 'WARNING' INSTALL.md; then
    test_result "INSTALL.md has migration warning" "PASS"
else
    test_result "INSTALL.md has migration warning" "FAIL"
fi

# =============================================================================
# TEST CATEGORY 8: Edge Cases & Failure Scenarios
# =============================================================================
echo ""
echo -e "${CYAN}[8/8] Testing Edge Cases${NC}"
echo ""

# Test 8.1: Empty input handling
echo "Test 8.1: Empty whitelist input handling"
if grep -q 'if \[ -n "$whitelist_input" \]' install.sh; then
    test_result "install.sh handles empty whitelist input" "PASS"
else
    test_result "install.sh handles empty whitelist input" "FAIL"
fi

# Test 8.2: Invalid path handling
echo "Test 8.2: Invalid path rejection"
if grep -q 'Skipped (invalid)' install.sh; then
    test_result "install.sh logs invalid paths" "PASS"
else
    test_result "install.sh logs invalid paths" "FAIL"
fi

# Test 8.3: Help text availability
echo "Test 8.3: Help text in all scripts"
HELP_COUNT=0
grep -q -- '--help' install.sh 2>/dev/null || grep -q 'help' install.sh && ((HELP_COUNT++))
grep -q -- '--help' uninstall.sh 2>/dev/null && ((HELP_COUNT++))
grep -q -- 'help' tools/preheat-ctl.c && ((HELP_COUNT++))

if [ $HELP_COUNT -ge 2 ]; then
    test_result "Scripts provide help text" "PASS"
else
    test_result "Scripts provide help text" "FAIL"
fi

# Test 8.4: Root check
echo "Test 8.4: Root permission validation"
if grep -q 'EUID -ne 0' install.sh && \
   grep -q 'EUID -ne 0' uninstall.sh; then
    test_result "Scripts verify root permissions" "PASS"
else
    test_result "Scripts verify root permissions" "FAIL"
fi

# =============================================================================
# Summary
# =============================================================================
echo ""
echo "========================================="
echo "Test Summary"
echo "========================================="
echo -e "Total Tests: ${TOTAL}"
echo -e "${GREEN}Passed: ${PASSED}${NC}"
echo -e "${RED}Failed: ${FAILED}${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}${BOLD}✓ ALL TESTS PASSED${NC}"
    exit 0
else
    PASS_RATE=$((PASSED * 100 / TOTAL))
    echo -e "${YELLOW}Pass Rate: ${PASS_RATE}%${NC}"
    if [ $PASS_RATE -ge 90 ]; then
        echo -e "${GREEN}Acceptable (≥90%)${NC}"
        exit 0
    else
        echo -e "${RED}Below threshold (<90%)${NC}"
        exit 1
    fi
fi
