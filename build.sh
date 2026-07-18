#!/usr/bin/env bash
# ============================================================================
# Team 106 build & deploy script
#
# Uses contest board config from: contest2026_106_VelaGoGoGo/board/contest_board/
# Linked to: vendor/openvela/boards/contest2026_106_board/
#
# Usage:
#   ./build.sh                incremental build (daily dev, ~10s)
#   ./build.sh full            full build (first time / after defconfig change)
#   ./build.sh flash           incremental build + flash to device
#   ./build.sh full-flash      full build + flash
# ============================================================================

set -eo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEAM_REPO="$SCRIPT_DIR"
WORKSPACE="$(cd "$TEAM_REPO/.." && pwd)"

# Contest board config (linked via manifest)
BOARD_CONFIG="vendor/openvela/boards/contest2026_106_board/configs/nsh"
NUTTX_DIR="$WORKSPACE/nuttx"

# Vendor paths for rcS.nsh (needs temporary override)
VENDOR_RCS="$WORKSPACE/vendor/allwinnertech/boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS.nsh"
TEAM_RCS="$TEAM_REPO/configs/rcS.nsh"
VENDOR_GIT="$WORKSPACE/vendor/allwinnertech"

export PATH="$WORKSPACE/prebuilts/build-tools/linux-x86_64/bin:$PATH"

die() { echo "ERROR: $*" >&2; exit 1; }

restore_vendor() {
    cd "$VENDOR_GIT"
    git checkout -- \
        "boards/r528/r528s3-gemini-s1/src/etc/init.d/rcS.nsh" \
        2>/dev/null || true
    echo "  [restored vendor rcS.nsh]"
}

do_full_build() {
    echo "=== Full build ==="

    # Apply team rcS.nsh (temporary — restored after build)
    if [ -f "$TEAM_RCS" ]; then
        cp "$TEAM_RCS" "$VENDOR_RCS"
        echo "  [applied team rcS.nsh]"
    fi

    # Build
    cd "$WORKSPACE"
    ./build.sh "$BOARD_CONFIG" -j8 || die "full build failed"

    # Restore vendor immediately
    restore_vendor
    echo "=== Full build done ==="
}

do_incremental_build() {
    echo "=== Incremental build ==="
    cd "$WORKSPACE"
    # envsetup.sh references NUTTX_DIR_NAME (must be set for -u mode)
    export NUTTX_DIR_NAME="${NUTTX_DIR_NAME:-nuttx}"
    source build/envsetup.sh 2>/dev/null || true
    make -C "$NUTTX_DIR" EXTRAFLAGS="-Wno-cpp -Wno-deprecated-declarations" -j8 || die "incremental build failed"
    echo "=== Incremental build done ==="
}

do_flash() {
    local vela="$NUTTX_DIR/vela.bin"
    [ -f "$vela" ] || die "$vela not found. Build first."

    echo "=== Flashing to device ==="
    adb push "$vela" /data/vela.bin
    adb shell "dd if=/data/vela.bin of=/dev/bootloader"
    adb shell reboot

    echo "=== Waiting for device (~15s) ==="
    sleep 12
    adb devices -l
    echo "=== Done ==="
}

# --- main ---
MODE="${1:-incremental}"

case "$MODE" in
    full)        do_full_build ;;
    flash)       do_incremental_build; do_flash ;;
    full-flash|flash-full) do_full_build; do_flash ;;
    *)           do_incremental_build ;;
esac
