#!/bin/bash
#
# Preheat Safe Update Script
# Updates preheat in-place with automatic rollback
#
# Phase 4: Safe In-Place Update System

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
DIM='\033[2m'
NC='\033[0m'

# Directories
BACKUP_DIR="/tmp/preheat-backup-$(date +%s)"
STATE_DIR="/usr/local/var/lib/preheat"
CONFIG_DIR="/etc/preheat.d"
MAIN_CONFIG="/usr/local/etc/preheat.conf"

# Cleanup on failure
cleanup_on_failure() {
    echo ""
    echo -e "${RED}${BOLD}✗ Update failed! Rolling back...${NC}"
    
    # Restore backup
    if [ -d "$BACKUP_DIR" ]; then
        [ -d "$BACKUP_DIR/state" ] && cp -a "$BACKUP_DIR/state/"* "$STATE_DIR/" 2>/dev/null
        [ -d "$BACKUP_DIR/config" ] && cp -a "$BACKUP_DIR/config/"* "$CONFIG_DIR/" 2>/dev/null
        [ -f "$BACKUP_DIR/preheat.conf" ] && cp "$BACKUP_DIR/preheat.conf" "$MAIN_CONFIG" 2>/dev/null
        
        # Restore old binaries
        [ -f "$BACKUP_DIR/preheat" ] && cp "$BACKUP_DIR/preheat" /usr/local/sbin/preheat
        [ -f "$BACKUP_DIR/preheat-ctl" ] && cp "$BACKUP_DIR/preheat-ctl" /usr/local/sbin/preheat-ctl
        
        echo -e "${GREEN}      ✓ Backup restored${NC}"
    fi
    
    # Restart daemon
    systemctl start preheat.service 2>/dev/null && \
        echo -e "${GREEN}      ✓ Service restarted${NC}" || \
        echo -e "${YELLOW}      ⚠ Service restart failed${NC}"
    
    echo ""
    echo -e "${RED}Rollback complete. System restored to previous state.${NC}"
    exit 1
}

trap cleanup_on_failure ERR

# Check root
if [[ $EUID -ne 0 ]]; then
    echo -e "${RED}${BOLD}✗ Error:${NC} Must run as root"
    echo -e "${DIM}  Try: ${CYAN}sudo $0${NC}"
    exit 1
fi

# Check dependencies
echo ""
echo -e "${CYAN}${BOLD}Preheat Safe Update${NC}"
echo ""

echo -e "${CYAN}Checking dependencies...${NC}"
for cmd in git autoconf automake pkg-config gcc make; do
    if ! command -v $cmd &> /dev/null; then
        echo -e "${RED}      ✗ Missing: $cmd${NC}"
        echo -e "${YELLOW}Install with: sudo apt-get install autoconf automake pkg-config build-essential${NC}"
        exit 1
    fi
done
echo -e "${GREEN}      ✓ All dependencies present${NC}"

# Step 1: Backup
echo ""
echo -e "${CYAN}[1/6]${NC} Creating backup..."
mkdir -p "$BACKUP_DIR"/{state,config}

[ -d "$STATE_DIR" ] && cp -a "$STATE_DIR/"* "$BACKUP_DIR/state/" 2>/dev/null || true
[ -d "$CONFIG_DIR" ] && cp -a "$CONFIG_DIR/"* "$BACKUP_DIR/config/" 2>/dev/null || true
[ -f "$MAIN_CONFIG" ] && cp "$MAIN_CONFIG" "$BACKUP_DIR/preheat.conf" 2>/dev/null || true

# Backup binaries
cp /usr/local/sbin/preheat "$BACKUP_DIR/preheat" 2>/dev/null || true
cp /usr/local/sbin/preheat-ctl "$BACKUP_DIR/preheat-ctl" 2>/dev/null || true

echo -e "${GREEN}      ✓ Backup created: $BACKUP_DIR${NC}"

# Step 2: Stop daemon
echo -e "${CYAN}[2/6]${NC} Stopping daemon..."
if systemctl is-active --quiet preheat.service; then
    systemctl stop preheat.service
    echo -e "${GREEN}      ✓ Daemon stopped${NC}"
else
    echo -e "${DIM}      Daemon not running${NC}"
fi

# Step 3: Download latest
echo -e "${CYAN}[3/6]${NC} Downloading latest version..."
TMPDIR=$(mktemp -d)

echo -e "${DIM}      Cloning repository...${NC}"
if ! git clone --quiet --depth 1 https://github.com/wasteddreams/preheat-linux.git "$TMPDIR/preheat" 2>/dev/null; then
    echo -e "${RED}      ✗ Failed to download from GitHub${NC}"
    echo -e "${YELLOW}      Check network connection${NC}"
    cleanup_on_failure
fi

cd "$TMPDIR/preheat"
NEW_VERSION=$(grep AC_INIT configure.ac | sed 's/.*\[\([0-9.]*\)\].*/\1/')
echo -e "${GREEN}      ✓ Downloaded v$NEW_VERSION${NC}"

# Step 4: Build
echo -e "${CYAN}[4/6]${NC} Building..."
echo -e "${DIM}      Running autoreconf...${NC}"
autoreconf --install --force > /dev/null 2>&1

echo -e "${DIM}      Configuring...${NC}"
./configure --quiet

echo -e "${DIM}      Compiling ($(nproc) cores)...${NC}"
make -j$(nproc) --quiet

echo -e "${GREEN}      ✓ Build successful${NC}"

# Step 5: Install
echo -e "${CYAN}[5/6]${NC} Installing..."
make install --quiet
systemctl daemon-reload
echo -e "${GREEN}      ✓ Installed${NC}"

# Step 6: Restore data and restart
echo -e "${CYAN}[6/6]${NC} Finalizing..."
# Data is preserved automatically (not overwritten by make install)

if systemctl is-enabled --quiet preheat.service 2>/dev/null; then
    systemctl start preheat.service
    sleep 1
    
    if systemctl is-active --quiet preheat.service; then
        echo -e "${GREEN}      ✓ Service running${NC}"
    else
        echo -e "${RED}      ✗ Service failed to start${NC}"
        cleanup_on_failure
    fi
else
    echo -e "${YELLOW}      ○ Service not enabled (manual start needed)${NC}"
fi

# Cleanup temp files
rm -rf "$TMPDIR"

# Success banner
echo ""
echo -e "${GREEN}${BOLD}╔════════════════════════════════════════════════════════════════╗${NC}"
echo -e "${GREEN}${BOLD}║                                                                ║${NC}"
echo -e "${GREEN}${BOLD}║                   ✓ UPDATE COMPLETE                            ║${NC}"
echo -e "${GREEN}${BOLD}║                                                                ║${NC}"
echo -e "${GREEN}${BOLD}║                  Now running v$NEW_VERSION$(printf '%*s' $((33-${#NEW_VERSION})) '')║${NC}"
echo -e "${GREEN}${BOLD}║                                                                ║${NC}"
echo -e "${GREEN}${BOLD}╚════════════════════════════════════════════════════════════════╝${NC}"
echo ""
echo -e "${GREEN}✓ All data preserved${NC}"
echo -e "${DIM}  Backup retained at: $BACKUP_DIR${NC}"
echo ""
