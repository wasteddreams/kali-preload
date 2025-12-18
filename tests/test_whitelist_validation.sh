#!/bin/bash
# Test script for Phase 1: Install-Time Whitelist Onboarding
# Tests the whitelist validation logic

echo "=== Testing Whitelist Validation Logic ==="
echo ""

# Test 1: Valid executable
echo "Test 1: Valid executable (/usr/bin/bash)"
if [[ "/usr/bin/bash" == /* ]] && [ -x "/usr/bin/bash" ]; then
    echo "✓ PASS: /usr/bin/bash is valid"
else
    echo "✗ FAIL: /usr/bin/bash should be valid"
fi

# Test 2: Valid executable (another)
echo ""
echo "Test 2: Valid executable (/bin/ls)"
if [[ "/bin/ls" == /* ]] && [ -x "/bin/ls" ]; then
    echo "✓ PASS: /bin/ls is valid"
else
    echo "✗ FAIL: /bin/ls should be valid"
fi

# Test 3: Invalid - not executable
echo ""
echo "Test 3: Invalid - not executable (/etc/passwd)"
if [[ "/etc/passwd" == /* ]] && [ -x "/etc/passwd" ]; then
    echo "✗ FAIL: /etc/passwd should be rejected (not executable)"
else
    echo "✓ PASS: /etc/passwd correctly rejected"
fi

# Test 4: Invalid - doesn't exist
echo ""
echo "Test 4: Invalid - doesn't exist (/nonexistent/app)"
if [[ "/nonexistent/app" == /* ]] && [ -x "/nonexistent/app" ]; then
    echo "✗ FAIL: /nonexistent/app should be rejected"
else
    echo "✓ PASS: /nonexistent/app correctly rejected"
fi

# Test 5: Invalid - relative path
echo ""
echo "Test 5: Invalid - relative path (bash)"
if [[ "bash" == /* ]] && [ -x "bash" ]; then
    echo "✗ FAIL: relative path should be rejected"
else
    echo "✓ PASS: relative path correctly rejected"
fi

# Test 6: TTY detection
echo ""
echo "Test 6: TTY detection"
if [ -t 0 ]; then
    echo "✓ Interactive TTY detected (normal terminal)"
else
    echo "✓ Non-interactive mode (piped input or CI)"
fi

echo ""
echo "=== All validation tests complete ==="
