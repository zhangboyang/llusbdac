#!/usr/bin/env python3
import sys
import subprocess
import os
import struct
import zlib
import traceback
import gc

if getattr(sys, "frozen", False):
    os.chdir(os.path.dirname(sys.executable))

def decode_audio(audio_file, fmt):
    if type(fmt) is not list:
        fmt = ["-f", fmt]
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", audio_file] + fmt + ["-vn", "-ac", "2", "pipe:1"], capture_output=True, check=True)
    return r.stdout

def convert_pcm(pcm_bytes, fmt_from, fmt_to):
    if type(fmt_to) is not list:
        fmt_to = ["-f", fmt_to]
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-f", fmt_from, "-vn", "-ac", "2", "-i", "pipe:0"] + fmt_to + ["-ac", "2", "pipe:1"], capture_output=True, input=pcm_bytes, check=True)
    return r.stdout

def calc_crc(data):
    return "%08x" % zlib.crc32(data)

def calc_time(frames, rate):
    hh, mm, ss, ms = (0, 0, 0, 0)
    if rate > 0:
        ss = frames // rate
        ms = frames % rate * 1000 // rate
        hh = ss / 3600
        ss %= 3600
        mm = ss / 60
        ss %= 60
        if hh > 99:
            hh, mm, ss, ms = (99, 99, 99, 999)
    return "%02d:%02d:%02d.%03d" % (hh, mm, ss, ms)

def pad_zero(length, align):
    return b"\0" * ((align - length % align) % align)

def trim_stereo_audio(samples, sample_bits):
    align = 2 * sample_bits // 8
    
    l = len(samples) - len(samples.lstrip(b"\0"))
    gc.collect()
    l = l // align * align
    
    r = len(samples.rstrip(b"\0"))
    gc.collect()
    r = (r + align - 1) // align * align
    
    return samples[l:r] if l < r else b''

table = []
def process(filename):
    print("  processing '%s'" % os.path.basename(filename), file=sys.stderr)
    line = "      ?       |       ?             ?             ?       | " + os.path.basename(filename)
    try:
        results = {}
        for sample_bits, fmt in [(16, "s16le"), (24, "s24le"), (32, "s32le")]:
            result = {}
            
            samples = decode_audio(filename, fmt)
            gc.collect()
            result["trunc_hash"] = convert_pcm(samples, fmt, ["-f", "md5", "-c:a", "pcm_f64le"])
            gc.collect()

            samples = trim_stereo_audio(samples, sample_bits)
            gc.collect()
            result["trim_crc"] = calc_crc(samples)
            result["trim_len"] = len(samples)
            del samples
            gc.collect()

            results[sample_bits] = result
        
        notrunc_hash = decode_audio(filename, ["-f", "md5", "-c:a", "pcm_f64le"])
        rate = struct.unpack("<I", decode_audio(filename, ["-f", "wav", "-c:a", "pcm_s32le"])[24:28])[0]
        gc.collect()

        line = " " + calc_time(results[32]["trim_len"] // 8, rate) + " | " + " ".join(map(lambda bits:
            "["+str(bits)+":"+results[bits]["trim_crc"].upper()+"]" if results[bits]["trunc_hash"] == notrunc_hash else
            " "+str(bits)+":"+results[bits]["trim_crc"]+" ", [16, 24, 32])) + " | " + os.path.basename(filename)

    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors="ignore"), file=sys.stderr)
    except FileNotFoundError:
        print("    ffmpeg not found", file=sys.stderr)
    except OSError:
        traceback.print_exc()
    finally:
        table.append(line)

for argv in sys.argv[1:]:
    if os.path.isdir(argv):
        for root, dirs, files in os.walk(argv):
            dirs.sort()
            files.sort()
            for name in files:
                process(os.path.join(root, name))
    else:
        process(argv)

print("--------------+-------------------------------------------+-----------------")
print("     TIME     |     S16LE        S24_3LE        S32LE     |      FILE")
print("--------------+-------------------------------------------+-----------------")
print("\n".join(table) if table else "                 (no results)")
print("--------------+-------------------------------------------+-----------------")

if os.name == "nt":
    os.system("pause")
