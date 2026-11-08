FROM debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential debootstrap qemu-system-x86 grub-pc-bin grub-common mtools dosfstools \
    e2fsprogs cpio xz-utils python3 ca-certificates kmod syslinux syslinux-common fdisk squashfs-tools p7zip-full \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
