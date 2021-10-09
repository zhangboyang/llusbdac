#!/usr/bin/env python3
import sys
import subprocess
import os
import zlib
import hashlib
import traceback
import gc

def decode_audio(audio_file, fmt):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", audio_file, "-f", fmt, "-ac", "2", "pipe:1"], capture_output=True, check=True)
    return r.stdout

def convert_pcm(pcm_bytes, fmt_from, fmt_to):
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-f", fmt_from, "-ac", "2", "-i", "pipe:0", "-f", fmt_to, "-ac", "2", "pipe:1"], capture_output=True, input=pcm_bytes, check=True)
    return r.stdout

def calc_crc(data):
    return "%08x" % zlib.crc32(data)

def calc_md5(data):
    return hashlib.md5(data).hexdigest()

def pad_zero(length, align):
    return b'\0' * ((align - length % align) % align)

def trim_stereo_audio(samples, sample_bits):
    align = 2*sample_bits//8
    samples = samples.lstrip(b'\0')
    samples = pad_zero(len(samples), align) + samples
    samples = samples.rstrip(b'\0')
    samples = samples + pad_zero(len(samples), align)
    return samples

table = []
def process(filename):
    print("  processing '%s'" % os.path.basename(filename), file=sys.stderr)
    try:
        results = {}
        for sample_bits, fmt in [(16,'s16le'), (24,'s24le'), (32,'s32le')]:
            result = {}
            
            samples = decode_audio(filename, fmt)
            gc.collect()
            result['orig_md5'] = calc_md5(samples)
            gc.collect()
            result['extend_md5'] = calc_md5(convert_pcm(samples, fmt, 'f64le'))
            gc.collect()
            #result['orig_crc'] = calc_crc(samples)
            #result['orig_len'] = len(samples)

            samples = trim_stereo_audio(samples, sample_bits)
            gc.collect()
            result['trim_crc'] = calc_crc(samples)
            gc.collect()
            result['trim_len'] = len(samples)
            gc.collect()

            results[sample_bits] = result
        
        best_md5 = calc_md5(decode_audio(filename, 'f64le'))
        gc.collect()

        table.append(' '.join(map(lambda bits:
            '['+str(bits)+':'+results[bits]['trim_crc'].upper()+']' if results[bits]['extend_md5'] == best_md5 else
            ' '+str(bits)+':'+results[bits]['trim_crc']+' ', [16, 24, 32])) + ' | ' + os.path.basename(filename))
    except subprocess.CalledProcessError as e:
        table.append('      ?             ?             ?       | ' + os.path.basename(filename))
        print(e.stderr.decode(errors='ignore'), file=sys.stderr)
    except OSError:
        table.append('      ?             ?             ?       | ' + os.path.basename(filename))
        traceback.print_exc()

for argv in sys.argv[1:]:
    if os.path.isdir(argv):
        for root, dirs, files in os.walk(argv, topdown=False):
            for name in files:
                process(os.path.join(root, name))
    else:
        process(argv)

print('------------------------------------------+------------------------------')
print('    S16LE    |   S24_3LE   |    S32LE     | file')
print('------------------------------------------+------------------------------')
print('\n'.join(table) if table else '      (no results)')
print('------------------------------------------+------------------------------')

if os.name == 'nt':
    os.system('pause')
