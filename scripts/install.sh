#!/bin/bash
#
# Preheat Installation Script
# https://github.com/wasteddreams/preheat
#
# This script provides an interactive installation with options
# for autostart configuration.
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Print functions
print_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[OK]${NC} $1"; }
print_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        print_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

# Check for required dependencies
check_dependencies() {
    print_info "Checking dependencies..."
    
    local missing_deps=()
    
    for cmd in make gcc pkg-config autoconf automake; do
        if ! command -v "$cmd" &> /dev/null; then
            missing_deps+=("$cmd")
        fi
    done
    
    # Check for libglib2.0-dev
    if ! pkg-config --exists glib-2.0 2>/dev/null; then
        missing_deps+=("libglib2.0-dev")
    fi
    
    if [[ ${#missing_deps[@]} -gt 0 ]]; then
        print_error "Missing dependencies: ${missing_deps[*]}"
        print_info "Install with: apt-get install ${missing_deps[*]}"
        exit 1
    fi
    
    print_success "All dependencies satisfied"
}

# Build the project
build_project() {
    print_info "Building preheat..."
    
    local LOG_FILE="/tmp/preheat-build-$$.log"
    
    # Generate build system if needed
    if [[ ! -f configure ]]; then
        printf "  ├─ Generating build system... "
        if autoreconf --install --force > "$LOG_FILE" 2>&1; then
            echo -e "${GREEN}done${NC}"
        else
            echo -e "${RED}failed${NC}"
            print_error "autoreconf failed. See $LOG_FILE for details."
            exit 1
        fi
    fi
    
    # Configure if not already done
    if [[ ! -f Makefile ]]; then
        printf "  ├─ Configuring... "
        if ./configure >> "$LOG_FILE" 2>&1; then
            echo -e "${GREEN}done${NC}"
        else
            echo -e "${RED}failed${NC}"
            print_error "configure failed. See $LOG_FILE for details."
            exit 1
        fi
    fi
    
    # Build
    printf "  └─ Compiling... "
    if make -j$(nproc) >> "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}done${NC}"
        rm -f "$LOG_FILE"  # Clean up on success
    else
        echo -e "${RED}failed${NC}"
        print_error "Build failed. See $LOG_FILE for details."
        exit 1
    fi
    
    print_success "Build completed successfully"
}

# Install the project
install_project() {
    print_info "Installing preheat..."
    
    local LOG_FILE="/tmp/preheat-install-$$.log"
    
    printf "  └─ Installing files... "
    if SKIP_SYSTEMD=1 make install > "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}done${NC}"
        rm -f "$LOG_FILE"
    else
        echo -e "${RED}failed${NC}"
        print_error "Installation failed. See $LOG_FILE for details."
        exit 1
    fi
    
    print_success "Files installed successfully"
}

# Configure systemd service
configure_systemd() {
    if ! command -v systemctl &> /dev/null; then
        print_warn "systemd not found, skipping service configuration"
        return 0
    fi
    
    # Reload systemd daemon
    print_info "Reloading systemd daemon..."
    systemctl daemon-reload
    
    # Ask user about autostart
    echo ""
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${YELLOW}  AUTOSTART CONFIGURATION${NC}"
    echo -e "${YELLOW}═══════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "Would you like to enable preheat to start automatically on boot?"
    echo ""
    echo "  [Y] Yes - Enable autostart and start service now (recommended)"
    echo "  [N] No  - Don't enable autostart, I'll start it manually"
    echo ""
    
    local choice
    while true; do
        read -p "Enable autostart? [Y/n]: " choice
        choice=${choice:-Y}  # Default to Y if empty
        
        case "$choice" in
            [Yy]* )
                print_info "Enabling preheat service..."
                systemctl enable preheat.service
                print_success "Service enabled for autostart"
                
                print_info "Starting preheat service..."
                systemctl start preheat.service
                print_success "Service started"
                
                echo ""
                print_info "Check status with: systemctl status preheat"
                break
                ;;
            [Nn]* )
                print_info "Skipping autostart configuration"
                echo ""
                print_info "To enable later, run:"
                echo "  sudo systemctl enable preheat"
                echo "  sudo systemctl start preheat"
                break
                ;;
            * )
                echo "Please answer Y or N"
                ;;
        esac
    done
}

# Print summary
print_summary() {
    echo ""
    echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  INSTALLATION COMPLETE${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "Installed files:"
    echo "  • Binary:   /usr/local/sbin/preheat"
    echo "  • CLI:      /usr/local/sbin/preheat-ctl"
    echo "  • Config:   /usr/local/etc/preheat.conf"
    echo "  • Service:  /usr/local/lib/systemd/system/preheat.service"
    echo ""
    echo "Useful commands:"
    echo "  • Status:   sudo systemctl status preheat"
    echo "  • Stop:     sudo systemctl stop preheat"
    echo "  • Logs:     sudo journalctl -u preheat -f"
    echo "  • Control:  sudo preheat-ctl help"
    echo ""
    echo "Documentation: README.md, CONFIGURATION.md"
    echo ""
}

# Non-interactive mode (for scripts/automation)
usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --autostart     Enable autostart without prompting"
    echo "  --no-autostart  Skip autostart without prompting"
    echo "  --help          Show this help message"
    echo ""
    exit 0
}

# Main
main() {
    local autostart_mode=""
    
    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --autostart)
                autostart_mode="yes"
                shift
                ;;
            --no-autostart)
                autostart_mode="no"
                shift
                ;;
            --help|-h)
                usage
                ;;
            *)
                print_error "Unknown option: $1"
                usage
                ;;
        esac
    done
    
    echo ""
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  PREHEAT INSTALLATION SCRIPT${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
    echo ""
    
    check_root
    check_dependencies
    build_project
    install_project
    
    # Handle systemd based on mode
    if [[ "$autostart_mode" == "yes" ]]; then
        if command -v systemctl &> /dev/null; then
            systemctl daemon-reload
            systemctl enable preheat.service
            systemctl start preheat.service
            print_success "Autostart enabled"
        fi
    elif [[ "$autostart_mode" == "no" ]]; then
        if command -v systemctl &> /dev/null; then
            systemctl daemon-reload
            print_info "Skipped autostart (use 'systemctl enable preheat' to enable later)"
        fi
    else
        configure_systemd
    fi
    
    print_summary
}

# Run main with all arguments
main "$@"
