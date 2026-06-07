#!/bin/bash
# Pin the whole QEMU process to host logical CPUs 0-3. On this i5-8350U the SMT
# sibling pairs are (0,4) (1,5) (2,6) (3,7), so 0-3 are four *distinct* physical
# cores: the 4 vCPUs get one physical core each and never share an SMT sibling,
# which is the main source of run-to-run benchmark jitter. Override the core set
# with the QEMU_CPUS env var if your host topology differs.
#
# For strict 1:1 vCPU<->core pinning (and to keep host work off these cores),
# boot the *host* with isolcpus=0-3 nohz_full=0-3 and pin each vCPU thread via
# libvirt vcpupin / QMP query-cpus-fast.
QEMU_CPUS="${QEMU_CPUS:-0-3}"

taskset -c "$QEMU_CPUS" \
./qemu/build/qemu-system-x86_64 \
    -nographic \
    -enable-kvm -cpu host \
    -smp 4,sockets=1,cores=4,threads=1 \
    -m 1G \
    -kernel linux/arch/x86/boot/bzImage \
    -initrd initramfs.cpio.gz \
    -append "console=ttyS0" \
    --fsdev local,security_model=none,id=fsdev0,path=/home/kevin/ring-buffer \
    -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=host_share
