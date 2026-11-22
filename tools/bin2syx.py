#!/usr/bin/env python3
"""Encode a flat C28x flash image into a Mother-32 firmware SysEx file.

Data words are packed three 6-bit groups per word (0x40 | bits 15-12, bits 11-6, bits 5-0).
Packet check values are the 16-bit sum of all words in the packet with the check field
taken as zero. The begin packet carries the application entry offset (word address
relative to 0x3F0000), then the image follows in 128-word pages.

usage: bin2syx.py image.bin out.syx --entry 0x0A26 [--pad 0x0000] [--skip-blank]
"""
import struct, sys

HEADER = bytes.fromhex('f0041400000100000000000001000200')
PAGE = 128

def enc_word(x):
    return bytes((0x40 | (x >> 12), (x >> 6) & 0x3F, x & 0x3F))

def packet(words_with_zero_check):
    w = list(words_with_zero_check)
    w[3] = sum(w) & 0xFFFF
    return w

def main():
    args = sys.argv[1:]
    entry = None; pad = 0x0000
    if '--entry' in args: entry = int(args[args.index('--entry') + 1], 0)
    if '--pad' in args: pad = int(args[args.index('--pad') + 1], 0)
    assert entry is not None, '--entry is required'
    img = open(args[0], 'rb').read()
    assert len(img) % 2 == 0
    words = [struct.unpack_from('<H', img, i)[0] for i in range(0, len(img), 2)]
    out = []
    out += packet([0x0001, 0x0006, 0x0100, 0, entry, 0x003F])
    # --skip-blank: leave out pages that are entirely 0xFFFF (already blank after ERASE).
    # The last page is always sent: it completes the update.
    skip_blank = '--skip-blank' in args
    last = ((len(words) - 1) // PAGE) * PAGE
    sent = 0
    for addr in range(0, len(words), PAGE):
        chunk = words[addr:addr + PAGE]
        n = len(chunk)
        if skip_blank and addr != last and all(x == 0xFFFF for x in chunk):
            continue
        chunk = chunk + [pad] * (PAGE - n)
        out += packet([0x0002, 0x0086, n, 0, addr, 0x003F] + chunk)
        sent += 1
    body = b''.join(enc_word(x) for x in out)
    open(args[1], 'wb').write(HEADER + body + b'\xf7')
    print(f"{len(words)} words -> {sent} of {(len(words)+PAGE-1)//PAGE} pages, entry 0x{entry:04x}, {len(body)+17} bytes")

if __name__ == '__main__':
    main()
