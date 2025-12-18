#!/bin/bash
#
# Preheat Reinstall Script
# Safely reinstalls preheat while preserving configuration and learned data
#
# Usage:
#   sudo bash reinstall.sh              # Preserve data (default)
#   sudo bash reinstall.sh --clean      # Fresh install (wipe all data)
#   sudo bash reinstall.sh --help       # Show help

set -e

# Colors and formatting
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# Parse arguments
CLEAN_INSTALL=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --clean)
            CLEAN_INSTALL=true
            shift
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Reinstall preheat daemon (uninstall + install)"
            echo ""
            echo "Options:"
            echo "  --clean        Fresh install (remove ALL data and config)"
            echo "  --help, -h     Show this help message"
            echo ""
            echo "Default behavior: Preserves all configuration and learned data"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Check root
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}${BOLD}✗ Error:${NC} This script must be run as root"
    echo -e "${DIM}  Try: ${CYAN}sudo $0${NC}"
    exit 1
fi

# Header
echo ""
echo -e "${CYAN}${BOLD}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}${BOLD}║                                                                ║${NC}"
echo -e "${CYAN}${BOLD}║                    PREHEAT REINSTALLER                         ║${NC}"
echo -e "${CYAN}${BOLD}║                                                                ║${NC}"
echo -e "${CYAN}${BOLD}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ "$CLEAN_INSTALL" = true ]; then
    echo -e "${YELLOW}Mode: CLEAN INSTALL (all data will be removed)${NC}"
else
    echo -e "${GREEN}Mode: UPGRADE (data will be preserved)${NC}"
fi
echo ""

# Confirm clean install
if [ "$CLEAN_INSTALL" = true ]; then
    echo -e "${YELLOW}⚠ WARNING: This will permanently delete all preheat data!${NC}"
    echo -e "${DIM}  Learned application patterns, whitelists, and configuration${NC}"
    echo ""
    read -p "$(echo -e ${BOLD}Are you absolutely sure? [y/N]:${NC} )" confirm
    confirm=${confirm:-N}
    
    if [[ ! "$confirm" =~ ^[Yy]$ ]]; then
        echo -e "${GREEN}Cancelled. Use without --clean to preserve data.${NC}"
        exit 0
    fi
    echo ""
fi

# Step 1: Uninstall
echo -e "${CYAN}${BOLD}[1/2]${NC} ${BOLD}Uninstalling current version${NC}"
echo ""

if [ -f "./uninstall.sh" ]; then
    if [ "$CLEAN_INSTALL" = true ]; then
        echo -e "${DIM}Running: bash uninstall.sh --purge-data${NC}"
        bash ./uninstall.sh --purge-data
    else
        echo -e "${DIM}Running: bash uninstall.sh --keep-data${NC}"
        bash ./uninstall.sh --keep-data
    fi
else
    echo -e "${YELLOW}⚠ uninstall.sh not found, attempting manual cleanup...${NC}"
    
    # Manual cleanup
    systemctl stop preheat.service 2>/dev/null || true
    systemctl disable preheat.service 2>/dev/null || true
    
    rm -f /usr/local/sbin/preheat
    rm -f /usr/local/sbin/preheat-ctl
    rm -f /usr/local/lib/systemd/system/preheat.service
    rm -f /run/preheat.pid
    
    systemctl daemon-reload 2>/dev/null
    
    if [ "$CLEAN_INSTALL" = true ]; then
        rm -rf /usr/local/var/lib/preheat
        rm -rf /etc/preheat.d
        rm -f /usr/local/etc/preheat.conf
        rm -f /usr/local/var/log/preheat.log
    fi
    
    echo -e "${GREEN}✓ Manual cleanup complete${NC}"
fi

echo ""

# Small delay to ensure clean state
sleep 1

# Step 2: Install
echo -e "${CYAN}${BOLD}[2/2]${NC} ${BOLD}Installing new version${NC}"
echo ""

if [ -f "./install.sh" ]; then
    if [ "$CLEAN_INSTALL" = true ]; then
        echo -e "${DIM}Running fresh installation...${NC}"
    else
        echo -e "${DIM}Running upgrade installation (preserving data)...${NC}"
    fi
    echo ""
    
    # Run install script
    bash ./install.sh
    
else
    echo -e "${RED}✗ Error: install.sh not found${NC}"
    echo -e "${YELLOW}Please ensure you're in the preheat source directory${NC}"
    exit 1
fi

# Success
echo ""
echo -e "${GREEN}${BOLD}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}${BOLD}║                                                                ║${NC}"
echo -e "${GREEN}${BOLD}║                 ✓ REINSTALLATION COMPLETE                      ║${NC}"
echo -e "${GREEN}${BOLD}║                                                                ║${NC}"
echo -e "${GREEN}${BOLD}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""

if [ "$CLEAN_INSTALL" = true ]; then
    echo -e "${DIM}Fresh installation completed. All previous data removed.${NC}"
else
    echo -e "${GREEN}✓ Upgrade complete - all data preserved${NC}"
    echo -e "${DIM}  Your learned patterns and configuration have been retained${NC}"
fi

echo ""
echo -e "${CYAN}Verify installation:${NC}"
echo -e "${DIM}  systemctl status preheat${NC}"
echo -e "${DIM}  preheat-ctl status${NC}"
echo ""
