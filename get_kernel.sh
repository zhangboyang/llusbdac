#!/bin/sh

arm-linux-gnueabihf-gcc -v || exit 1

# https://oss.sony.net/Products/Linux/Audio/NW-ZX300G.html
while ! (echo '16e5edbda808a6d3d53187509377779a0b6a7940aa2b0f079f35ce7f3a103e97  linux-kernel-3.10.26.tar.gz' | sha256sum -c); do
  wget -O linux-kernel-3.10.26.tar.gz 'https://prodgpl.blob.core.windows.net/download/Audio/NW-ZX300A/linux-kernel-3.10.26.tar.gz' || exit 1
done

(rm -rf kernel && mkdir kernel && tar xzf linux-kernel-3.10.26.tar.gz -C kernel) || exit 1

(cd kernel &&
  cp arch/arm/configs/BBDMP3_linux_debug_defconfig .config && 
  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- olddefconfig &&
  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- prepare &&
  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- scripts) || exit 1

echo
echo '=== SONY KERNEL PREPARED OK ==='
echo
exit 0
