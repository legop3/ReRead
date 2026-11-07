FROM debian:bookworm-slim
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    debootstrap qemu-system-x86 grub-pc-bin grub-common mtools dosfstools \
    e2fsprogs cpio xz-utils python3 ca-certificates kmod syslinux syslinux-common fdisk \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
