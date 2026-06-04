#!/bin/bash
#
# setup.sh — One-shot build of the "High-Performance Ring Buffer Char Driver"
#            development environment.
#
# Follows the procedure in doc/doc.md:
#   1. Install prerequisite packages
#   2. Clone + build QEMU (v11.0.1)
#   3. Clone + build the Linux kernel (v7.0)
#   4. Clone + build Busybox (1_36_0) and pack the initramfs
#
# Idempotent: existing repos are not re-cloned, and completed build
# steps are skipped, so the script is safe to re-run after a failure.

set -euo pipefail

# Use the script's own directory as the project root (not a hard-coded path).
PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
JOBS="$(nproc)"

QEMU_TAG="v11.0.1"
LINUX_TAG="v7.0"
BUSYBOX_TAG="1_36_0"

log()  { printf '\n\033[1;32m==> %s\033[0m\n' "$*"; }
skip() { printf '\033[1;33m--> skip: %s\033[0m\n' "$*"; }

# ---------------------------------------------------------------------------
# 0. Prerequisite packages
# ---------------------------------------------------------------------------
install_deps() {
    log "0. Installing prerequisite packages (requires sudo)"
    sudo apt update
    sudo apt install -y git build-essential ninja-build meson \
        libglib2.0-dev libpixman-1-dev libkmod-dev libasound2-dev \
        libslirp-dev libcap-ng-dev libfdt-dev
}

# ---------------------------------------------------------------------------
# 1. Fetch and build the QEMU system emulator
# ---------------------------------------------------------------------------
build_qemu() {
    log "1. QEMU ($QEMU_TAG)"
    cd "$PROJECT_ROOT"
    if [ ! -d qemu/.git ]; then
        git clone -b "$QEMU_TAG" --depth 1 https://gitlab.com/qemu-project/qemu.git
    else
        skip "qemu already present"
    fi

    cd "$PROJECT_ROOT/qemu"
    if [ ! -x build/qemu-system-x86_64 ]; then
        ./configure --target-list=x86_64-softmmu
        make -j"$JOBS"
    else
        skip "qemu-system-x86_64 already built"
    fi
}

# ---------------------------------------------------------------------------
# 2. Build the Linux kernel
# ---------------------------------------------------------------------------
build_linux() {
    log "2. Linux kernel ($LINUX_TAG)"
    cd "$PROJECT_ROOT"
    if [ ! -d linux/.git ]; then
        git clone -b "$LINUX_TAG" --depth 1 https://github.com/torvalds/linux.git
    else
        skip "linux already present"
    fi

    cd "$PROJECT_ROOT/linux"
    if [ ! -f arch/x86/boot/bzImage ]; then
        [ -f .config ] || make defconfig
        make -j"$JOBS"
    else
        skip "bzImage already built"
    fi
}

# ---------------------------------------------------------------------------
# 3. Build Busybox and pack the initramfs
# ---------------------------------------------------------------------------
build_busybox() {
    log "3. Busybox ($BUSYBOX_TAG) + initramfs"
    cd "$PROJECT_ROOT"
    if [ ! -d busybox/.git ]; then
        git clone -b "$BUSYBOX_TAG" --depth 1 https://github.com/mirror/busybox.git
    else
        skip "busybox already present"
    fi

    cd "$PROJECT_ROOT/busybox"
    make defconfig
    # 1. Must build statically: the rootfs has no glibc shared libraries.
    sed -i 's/# CONFIG_STATIC is not set/CONFIG_STATIC=y/' .config
    # 2. Disable tc (Traffic Control): newer kernels dropped the CBQ headers it needs.
    sed -i 's/CONFIG_TC=y/# CONFIG_TC is not set/' .config
    make -j"$JOBS" install

    log "Creating the rootfs layout"
    rm -rf rootfs
    mkdir -p rootfs/{bin,sbin,etc,proc,sys,usr/bin,usr/sbin,dev,sys/kernel/debug,sys/kernel/tracing}
    cp -a _install/* rootfs/

    log "Writing the PID 1 (init) script"
    cat << 'EOF' > rootfs/init
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sys /sys
mount -t devtmpfs dev /dev
mount -t debugfs none /sys/kernel/debug
mount -t tracefs none /sys/kernel/tracing

mkdir -p /mnt
mount -t 9p -o trans=virtio,version=9p2000.L host_share /mnt

echo "=========================================="
echo " Hello! Welcome to Linux Shell.  "
echo "=========================================="

exec setsid sh -c 'exec sh </dev/ttyS0 >/dev/ttyS0 2>&1'
EOF
    chmod +x rootfs/init

    log "Packing initramfs.cpio.gz into the project root"
    ( cd rootfs && find . -print0 \
        | cpio --null -ov --format=newc \
        | gzip -9 > "$PROJECT_ROOT/initramfs.cpio.gz" )
}

# ---------------------------------------------------------------------------
main() {
    install_deps
    build_qemu
    build_linux
    build_busybox

    log "Done! Run ./run.sh to boot QEMU and enter the guest environment."
}

main "$@"
