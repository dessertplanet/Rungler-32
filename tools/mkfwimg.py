#!/usr/bin/env python3
"""Turn the linked firmware into a Mother-32 firmware SysEx file.

usage: mkfwimg.py <build_dir> <name>      (expects <name>.out and <name>.map in build_dir)

Writes to build_dir:
  <name>_app.bin   application region 0x3F0000-0x3F5FB9 (0x5FBA words, little-endian, 0xFFFF fill)
  <name>.syx       firmware update file: send after ERASE twice, like an official update
Checks that nothing is placed outside the application region.
"""
import os, re, struct, subprocess, sys, platform

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CGT = os.environ.get('CGT', os.path.expanduser('~/ti/ti-cgt-c2000_22.6.1.LTS'))
BASE, APP_WORDS = 0x3F0000, 0x5FBA

def tool(name):
    cmd = [os.path.join(CGT, 'bin', name)]
    if platform.system() == 'Darwin' and platform.machine() == 'arm64':
        cmd = ['arch', '-x86_64'] + cmd
    return cmd

def main():
    bdir, name = sys.argv[1], sys.argv[2]
    out = os.path.join(bdir, name + '.out')
    mapf = open(os.path.join(bdir, name + '.map')).read()
    entry = int(re.search(r'ENTRY POINT SYMBOL: "_c_int00"\s+address:\s+([0-9a-f]+)', mapf).group(1), 16)
    assert BASE <= entry < BASE + APP_WORDS, f"entry {entry:#x} outside the app region"
    # every initialised section must lie inside the app region (RAM sections only as run addresses)
    for m in re.finditer(r'^\S*\s+0\s+(003f[0-9a-f]{4})\s+([0-9a-f]{8})', mapf, re.M):
        a, n = int(m.group(1), 16), int(m.group(2), 16)
        assert BASE <= a and a + n <= BASE + APP_WORDS, f"section at {a:#x}+{n:#x} leaves the app region"
    romcmd = os.path.join(bdir, 'app_rom.cmd')
    open(romcmd, 'w').write("ROMS { APP: origin = 0x7E0000, length = 0xBF74, romwidth = 16 }\n")
    binf = os.path.join(bdir, name + '_app.bin')
    subprocess.run(tool('hex2000') + [out, '--image', '--binary', '--memwidth=16', '--romwidth=16',
                   '--fill=0xFFFF', '--order=LS', '-o', binf, romcmd], check=True, stdout=subprocess.DEVNULL)
    data = open(binf, 'rb').read()
    assert len(data) == APP_WORDS * 2, f"app image is {len(data)} bytes, expected {APP_WORDS*2}"
    w = [struct.unpack_from('<H', data, i)[0] for i in range(0, len(data), 2)]
    used = max(i for i, x in enumerate(w) if x != 0xFFFF) + 1
    syx = os.path.join(bdir, name + '.syx')
    subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'bin2syx.py'), binf, syx,
                    '--entry', hex(entry - BASE)], check=True, stdout=subprocess.DEVNULL)
    print(f"app image: {used} of {APP_WORDS} words used, entry _c_int00 = {entry:#x}")
    print(f"  {syx}")

if __name__ == '__main__':
    main()
