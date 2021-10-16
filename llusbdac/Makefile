llusbdac-y := loader.o uac2.o gui.o player.o font_ter16x32.o crc32.o

obj-m += llusbdac.o
ccflags-y := -DDEBUG -O2 -std=gnu99 -Wno-declaration-after-statement -Werror

all:
	(cd ../safeloader; make)
	(cd ../panic2screen; make)
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	arm-linux-gnueabihf-strip -g llusbdac.ko
	sha256sum llusbdac.ko > llusbdac.sha256

run: all
	(cd ../panic2screen; make run)
	adb shell rm -f /tmp/llusbdac.ko
	adb push llusbdac.ko /tmp/llusbdac.ko
	adb shell rmmod llusbdac
	adb wait-for-usb-device
	adb shell dmesg -c
	adb shell insmod /tmp/llusbdac.ko
	adb wait-for-usb-device
	adb shell dmesg -c

clean:
	(cd ../safeloader; make clean)
	(cd ../panic2screen; make clean)
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- clean
	rm -f llusbdac.sha256
