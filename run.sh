#!/bin/bash
./qemu/build/qemu-system-x86_64 \
    -nographic \
    -enable-kvm -cpu host \
    -smp 4 \
    -m 1G \
    -kernel linux/arch/x86/boot/bzImage \
    -initrd initramfs.cpio.gz \
    -append "console=ttyS0" \
    --fsdev local,security_model=none,id=fsdev0,path=/home/kevin/ring-buffer \
    -device virtio-9p-pci,id=fs0,fsdev=fsdev0,mount_tag=host_share
