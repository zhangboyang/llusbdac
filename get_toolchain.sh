#!/bin/sh

# https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/
while ! (echo '22914118fd963f953824b58107015c6953b5bbdccbdcf25ad9fd9a2f9f11ac07  gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz' | sha256sum -c); do
  wget -O gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz 'https://releases.linaro.org/components/toolchain/binaries/4.9-2017.01/arm-linux-gnueabihf/gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz' || exit 1
done

(rm -rf toolchain && tar xJf gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf.tar.xz && mv gcc-linaro-4.9.4-2017.01-x86_64_arm-linux-gnueabihf toolchain) || exit 1

echo "export PATH=$(pwd)/toolchain/bin:\$PATH" > setpath

echo
echo "=== TO ADD COMPILER TO YOUR \$PATH, PLEASE RUN ==="
echo "    . setpath"
echo
exit 0
