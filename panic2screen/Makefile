panic2screen-y += font_6x8.o print.o
obj-m += panic2screen.o
ccflags-y := -std=gnu99 -Wno-declaration-after-statement -Werror

all:
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
	arm-linux-gnueabihf-strip -g panic2screen.ko

run: all
	adb shell rm -f /tmp/panic2screen.ko
	adb push panic2screen.ko /tmp/panic2screen.ko
	adb shell rmmod panic2screen
	adb shell dmesg -c
	adb shell insmod /tmp/panic2screen.ko
	adb shell dmesg -c

clean:
	make -C ../kernel M=$(PWD) ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- clean
