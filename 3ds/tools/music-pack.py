#!/usr/bin/env python3
"""Lossless FPC2: stereo PCM16/32000 Hz, second differences, then deterministic gzip.

The 12-byte decoded header is magic, PCM byte count and sample rate (little endian).
Each channel predicts 2*previous-older, modulo 65536; both histories start at zero.
No quantization, sample-rate conversion or DSP gain is applied during packaging.
"""
import array,gzip,struct,sys

def encode(pcm):
    if not pcm or len(pcm)%4 or len(pcm)>32*1024*1024:raise ValueError('Invalid stereo PCM length')
    samples=array.array('H');samples.frombytes(pcm)
    if sys.byteorder!='little':samples.byteswap()
    previous=[0,0];older=[0,0]
    for i,value in enumerate(samples):
        channel=i&1
        samples[i]=(value-2*previous[channel]+older[channel])&65535
        older[channel]=previous[channel];previous[channel]=value
    if sys.byteorder!='little':samples.byteswap()
    return gzip.compress(struct.pack('<4sII',b'FPC2',len(pcm),32000)+samples.tobytes(),compresslevel=9,mtime=0)
