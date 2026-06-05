#!/usr/bin/env bash
# install.sh — Build and install vEB Port Allocator daemon
# Run as root: sudo ./scripts/install.sh
# MIT License

set -euo pipefail

INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
CONFIG_DIR="/etc/portd"
SERVICE_USER="portd"
SERVICE_GROUP="portd"
BUILD_DIR="$(mktemp -d)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
info()  { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error() { echo -e "${RED}[ERROR]${NC} $*" >&2; exit 1; }

# ── Checks ────────────────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || error "This script must be run as root (sudo $0)"

command -v cmake  &>/dev/null || error "cmake not found. Install: apt install cmake"
command -v gcc    &>/dev/null || error "gcc not found.   Install: apt install build-essential"
command -v make   &>/dev/null || error "make not found.  Install: apt install build-essential"

# ── Build ─────────────────────────────────────────────────────────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

info "Building in $BUILD_DIR ..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_INSTALL_SYSCONFDIR=/etc \
    -DCMAKE_INSTALL_SBINDIR="$INSTALL_PREFIX/sbin" \
    -DCMAKE_INSTALL_LIBDIR="$INSTALL_PREFIX/lib" \
    -DCMAKE_INSTALL_INCLUDEDIR="$INSTALL_PREFIX/include" \
    -Wno-dev -DCMAKE_C_FLAGS="-O2" \
    2>&1 | tail -5

make -C "$BUILD_DIR" -j"$(nproc)"
info "Build complete."

# ── Run tests ─────────────────────────────────────────────────────────────
info "Running tests..."
(cd "$BUILD_DIR" && ctest --output-on-failure -R "veb_unit|manager_unit") \
    || warn "Some tests failed — proceeding anyway"

# ── Create system user ────────────────────────────────────────────────────
if ! id "$SERVICE_USER" &>/dev/null; then
    useradd --system --no-create-home --shell /usr/sbin/nologin \
        --comment "portd daemon" "$SERVICE_USER"
    info "Created system user '$SERVICE_USER'"
fi

# ── Install binaries ──────────────────────────────────────────────────────
make -C "$BUILD_DIR" install
info "Installed portd, portcli, libraries and headers."

# ── Config ────────────────────────────────────────────────────────────────
mkdir -p "$CONFIG_DIR"
if [[ ! -f "$CONFIG_DIR/config.json" ]]; then
    cp "$SCRIPT_DIR/sample_config.json" "$CONFIG_DIR/config.json"
    chmod 640 "$CONFIG_DIR/config.json"
    chown root:"$SERVICE_GROUP" "$CONFIG_DIR/config.json"
    info "Installed default config to $CONFIG_DIR/config.json"
else
    warn "Config already exists at $CONFIG_DIR/config.json — not overwriting."
fi

# ── Runtime directory ─────────────────────────────────────────────────────
mkdir -p /var/run/portd
chown "$SERVICE_USER":"$SERVICE_GROUP" /var/run/portd
chmod 750 /var/run/portd

# ── systemd ───────────────────────────────────────────────────────────────
if command -v systemctl &>/dev/null; then
    UNIT_DIR="$INSTALL_PREFIX/lib/systemd/system"
    if [[ -f "$UNIT_DIR/portd.service" ]]; then
        systemctl daemon-reload
        systemctl enable portd.service
        info "systemd unit installed and enabled."
        info "Start with: sudo systemctl start portd"
    fi
else
    warn "systemctl not found — skipping systemd setup."
fi

# ── Cleanup ───────────────────────────────────────────────────────────────
rm -rf "$BUILD_DIR"

info "Installation complete!"
info ""
info "Next steps:"
info "  1. Review/edit config:   $CONFIG_DIR/config.json"
info "  2. Start daemon:         sudo systemctl start portd"
info "  3. Check status:         sudo systemctl status portd"
info "  4. Test connectivity:    portcli ping"
