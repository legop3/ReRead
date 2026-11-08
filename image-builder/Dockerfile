FROM debian:bookworm-slim
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential gcc-multilib g++-multilib libusb-1.0-0-dev:i386 libjpeg62-turbo-dev:i386 debootstrap qemu-system-x86 grub-pc-bin grub-common mtools dosfstools \
    e2fsprogs cpio xz-utils python3 ca-certificates kmod syslinux syslinux-common fdisk squashfs-tools p7zip-full \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
