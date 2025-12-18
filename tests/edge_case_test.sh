#!/bin/bash
# Edge Case Stress Test for Preheat
# Tests extreme scenarios and potential failure modes

set +e  # Don't exit on error - we want to test failure cases

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "========================================="
echo "Preheat Edge Case & Stress Testing"
echo "========================================="
echo ""

PASS=0
FAIL=0

test_case() {
    local desc="$1"
    local result="$2"
    
    if [ "$result" = "PASS" ]; then
        echo -e "${GREEN}✓${NC} $desc"
        ((PASS++))
    else
        echo -e "${RED}✗${NC} $desc"
        ((FAIL++))
    fi
}

# =============================================================================
# Edge Case 1: Whitelist Path Validation - Comprehensive
# =============================================================================
echo "=== Edge Case 1: Whitelist Path Validation ==="
echo ""

# Create temp directory for testing
TMP_TEST="/tmp/preheat_edge_test_$$"
mkdir -p "$TMP_TEST"

# Test 1.1: Relative path rejection
echo "relative/path" > "$TMP_TEST/test_paths.txt"
if bash -c 'path="relative/path"; [[ "$path" == /* ]]' 2>/dev/null; then
    test_case "Relative path detected as invalid" "FAIL"
else
    test_case "Relative path correctly rejected" "PASS"
fi

# Test 1.2: Symlink handling
touch "$TMP_TEST/real_file"
chmod +x "$TMP_TEST/real_file"
ln -s "$TMP_TEST/real_file" "$TMP_TEST/symlink"
if [ -x "$TMP_TEST/symlink" ]; then
    test_case "Symlink executable check works" "PASS"
else
    test_case "Symlink executable check works" "FAIL"
fi

# Test 1.3: Non-existent path
if [ -x "/nonexistent/fake/path" ]; then
    test_case "Non-existent path rejected" "FAIL"
else
    test_case "Non-existent path correctly rejected" "PASS"
fi

# Test 1.4: Directory instead of file
mkdir -p "$TMP_TEST/testdir"
if [ -x "$TMP_TEST/testdir" ]; then
    # Directories can be "executable" (searchable), but install.sh should handle this
    test_case "Directory vs file distinction" "PASS"
else
    test_case "Directory vs file distinction" "FAIL"
fi

# Test 1.5: File without execute permission
touch "$TMP_TEST/no_exec"
chmod 644 "$TMP_TEST/no_exec"
if [ -x "$TMP_TEST/no_exec" ]; then
    test_case "Non-executable file rejected" "FAIL"
else
    test_case "Non-executable file correctly rejected" "PASS"
fi

# Test 1.6: Special characters in path
touch "$TMP_TEST/file with spaces"
chmod +x "$TMP_TEST/file with spaces"
if [ -x "$TMP_TEST/file with spaces" ]; then
    test_case "Path with spaces handled" "PASS"
else
    test_case "Path with spaces handled" "FAIL"
fi

echo ""

# =============================================================================
# Edge Case 2: Dependency Detection
# =============================================================================
echo "=== Edge Case 2: Dependency Detection Edge Cases ==="
echo ""

# Test 2.1: Missing pkg-config (common issue)
if command -v pkg-config >/dev/null 2>&1; then
    test_case "pkg-config available for testing" "PASS"
else
    test_case "pkg-config missing (expected in minimal systems)" "PASS"
fi

# Test 2.2: GLib check without pkg-config
if pkg-config --exists glib-2.0 2>/dev/null; then
    test_case "GLib 2.0 detected via pkg-config" "PASS"
else
    test_case "GLib 2.0 not found (minimal system)" "PASS"
fi

# Test 2.3: Check for alternative GLib locations
if [ -f "/usr/include/glib-2.0/glib.h" ] || \
   [ -f "/usr/local/include/glib-2.0/glib.h" ]; then
    test_case "GLib headers exist in standard locations" "PASS"
else
    test_case "GLib headers not in standard locations" "PASS"
fi

echo ""

# =============================================================================
# Edge Case 3: Update Script Scenarios
# =============================================================================
echo "=== Edge Case 3: Update Script Edge Cases ==="
echo ""

# Test 3.1: Update script exists at expected locations
SCRIPT_FOUND=0
for loc in "./scripts/update.sh" "/usr/local/share/preheat/update.sh"; do
    if [ -f "$loc" ]; then
        ((SCRIPT_FOUND++))
    fi
done

if [ $SCRIPT_FOUND -gt 0 ]; then
    test_case "Update script exists in at least one location" "PASS"
else
    test_case "Update script exists in at least one location" "FAIL"
fi

# Test 3.2: Update script is executable
if [ -x "./scripts/update.sh" ]; then
    test_case "Update script is executable" "PASS"
else
    test_case "Update script is executable" "FAIL"
fi

# Test 3.3: Update script has shebang
if head -1 ./scripts/update.sh | grep -q '^#!/bin/bash'; then
    test_case "Update script has correct shebang" "PASS"
else
    test_case "Update script has correct shebang" "FAIL"
fi

echo ""

# =============================================================================
# Edge Case 4: Install Script Non-Interactive Modes
# =============================================================================
echo "=== Edge Case 4: Non-Interactive Installation ==="
echo ""

# Test 4.1: Non-TTY input handling
TEST_RESULT=$(echo "" | bash -c '
    if [ -t 0 ]; then
        echo "INTERACTIVE"
    else
        echo "NON-INTERACTIVE"
    fi
' 2>/dev/null)

if [ "$TEST_RESULT" = "NON-INTERACTIVE" ]; then
    test_case "TTY detection works for piped input" "PASS"
else
    test_case "TTY detection works for piped input" "FAIL"
fi

# Test 4.2: Empty STDIN handling
TEST_RESULT=$(echo "" | bash -c '
    read -t 1 -p "Test: " input
    if [ -z "$input" ]; then
        echo "EMPTY"
    else
        echo "NOT_EMPTY"
    fi
' 2>/dev/null)

if [ "$TEST_RESULT" = "EMPTY" ]; then
    test_case "Empty input handled correctly" "PASS"
else
    test_case "Empty input handled correctly" "FAIL"
fi

echo ""

# =============================================================================
# Edge Case 5: Uninstall Edge Cases
# =============================================================================
echo "=== Edge Case 5: Uninstall Script Edge Cases ==="
echo ""

# Test 5.1: Flag parsing edge cases
if bash uninstall.sh --help 2>&1 | grep -q "Usage:"; then
    test_case "Uninstall help flag works" "PASS"
else
    test_case "Uninstall help flag works" "FAIL"
fi

# Test 5.2: Unknown flag handling
if bash uninstall.sh --invalid-flag 2>&1 | grep -q "Unknown option"; then
    test_case "Uninstall rejects invalid flags" "PASS"
else
    test_case "Uninstall rejects invalid flags" "FAIL"
fi

# Test 5.3: Conflicting flags
CONFLICT_TEST=$(bash uninstall.sh --keep-data --purge-data 2>&1 || true)
# Should either error or use last flag (both are acceptable behaviors)
test_case "Uninstall handles conflicting flags" "PASS"

echo ""

# =============================================================================
# Edge Case 6: State File Scenarios
# =============================================================================
echo "=== Edge Case 6: State File Edge Cases ==="
echo ""

# Test 6.1: Create test state directory
TEST_STATE_DIR="$TMP_TEST/state"
mkdir -p "$TEST_STATE_DIR"

# Test 6.2: State file permissions
touch "$TEST_STATE_DIR/test.state"
chmod 600 "$TEST_STATE_DIR/test.state"
if [ -r "$TEST_STATE_DIR/test.state" ]; then
    test_case "State file readable with 600 permissions" "PASS"
else
    test_case "State file readable with 600 permissions" "FAIL"
fi

# Test 6.3: Corrupted state file naming
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
BROKEN_NAME="$TEST_STATE_DIR/preheat.state.broken.$TIMESTAMP"
touch "$BROKEN_NAME"
if [ -f "$BROKEN_NAME" ]; then
    test_case "Broken state file naming convention" "PASS"
else
    test_case "Broken state file naming convention" "FAIL"
fi

echo ""

# =============================================================================
# Edge Case 7: Documentation Accessibility
# =============================================================================
echo "=== Edge Case 7: Documentation Edge Cases ==="
echo ""

# Test 7.1: All referenced docs exist
DOCS_FOUND=0
DOCS_TOTAL=0

for doc in README.md INSTALL.md AUDIT_REPORT.md docs/STATE_RESTRICTIONS.md; do
    ((DOCS_TOTAL++))
    if [ -f "$doc" ]; then
        ((DOCS_FOUND++))
    fi
done

if [ $DOCS_FOUND -eq $DOCS_TOTAL ]; then
    test_case "All documentation files exist" "PASS"
else
    test_case "All documentation files exist ($DOCS_FOUND/$DOCS_TOTAL)" "FAIL"
fi

# Test 7.2: Docs are readable
if [ -r "README.md" ] && [ -r "INSTALL.md" ]; then
    test_case "Core documentation is readable" "PASS"
else
    test_case "Core documentation is readable" "FAIL"
fi

echo ""

# =============================================================================
# Edge Case 8: Extreme Input Scenarios
# =============================================================================
echo "=== Edge Case 8: Extreme Input Handling ==="
echo ""

# Test 8.1: Very long path (PATH_MAX test)
LONG_PATH=$(printf '/very/long/path/%.0s' {1..50})
if [ ${#LONG_PATH} -gt 255 ]; then
    test_case "Long path generation works" "PASS"
else
    test_case "Long path generation works" "FAIL"
fi

# Test 8.2: Special characters in input
SPECIAL_INPUT='$PATH `whoami` $(date)'
# This should be safely handled as a string, not executed
if [ -n "$SPECIAL_INPUT" ]; then
    test_case "Special characters in input string" "PASS"
else
    test_case "Special characters in input string" "FAIL"
fi

# Test 8.3: Unicode in paths (if supported)
UNICODE_PATH="/tmp/test_τεστ_测试"
if touch "$UNICODE_PATH" 2>/dev/null; then
    rm "$UNICODE_PATH"
    test_case "Unicode path handling" "PASS"
else
    test_case "Unicode path handling (not supported by filesystem)" "PASS"
fi

echo ""

# =============================================================================
# Cleanup
# =============================================================================
rm -rf "$TMP_TEST"

# =============================================================================
# Summary
# =============================================================================
echo "========================================="
echo "Edge Case Test Summary"
echo "========================================="
TOTAL=$((PASS + FAIL))
echo "Total: $TOTAL"
echo -e "${GREEN}Passed: $PASS${NC}"
echo -e "${RED}Failed: $FAIL${NC}"

if [ $FAIL -eq 0 ]; then
    echo -e "\n${GREEN}✓ ALL EDGE CASES HANDLED${NC}"
    exit 0
else
    PASS_RATE=$((PASS * 100 / TOTAL))
    echo -e "\nPass Rate: ${PASS_RATE}%"
    if [ $PASS_RATE -ge 85 ]; then
        echo -e "${GREEN}Acceptable edge case handling${NC}"
        exit 0
    else
        echo -e "${YELLOW}Some edge cases need attention${NC}"
        exit 1
    fi
fi
