FROM debian:bookworm-slim
RUN dpkg --add-architecture i386 && apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential gcc-multilib g++-multilib libusb-1.0-0-dev:i386 libjpeg62-turbo-dev:i386 qtbase5-dev:i386 libglib2.0-dev:i386 pkg-config debootstrap qemu-system-x86 grub-pc-bin grub-common mtools dosfstools \
    apt-utils gnupg e2fsprogs cpio xz-utils python3 python3-dbus python3-gi ca-certificates kmod syslinux syslinux-common fdisk squashfs-tools p7zip-full \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
