obj-m += safeloader.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -Werror

all:
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	arm-linux-gnueabihf-strip -g safeloader.ko
	sha256sum safeloader.ko > safeloader.sha256

run: all
	adb shell rm -f /tmp/safeloader.ko
	adb push safeloader.ko /tmp/safeloader.ko
	adb shell rmmod safeloader
	adb shell dmesg -c
	adb shell insmod /tmp/safeloader.ko
	adb shell dmesg -c

clean:
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- clean
	rm -f safeloader.sha256
