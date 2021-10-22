# LLUSBDAC

Low-latency (relatively) USB DAC for Sony NW-ZX300 Series

## User Guide

* [English](userguide/USERGUIDE_EN.md)
* [中文](userguide/USERGUIDE_ZH.md)

## Features

* Relatively low-latency output (about 50ms)
* Show audio sample rate, bit depth, and CRC32 checksum
* Works as a USB Audio Class 2.0 device
* Only supports PCM audio
* Only tested on model NW-ZX300A currently

<img src="userguide/page1.png" width="216" height="176"/>

## Build

You can use installer to enable [ADB](https://developer.android.com/studio/command-line/adb), then test your build with following commands:

```
./get_toolchain.sh
. setpath
./get_kernel.sh
cd llusbdac
make
make run
```

## License

GPLv2
