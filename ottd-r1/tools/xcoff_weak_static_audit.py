#!/usr/bin/env python3
# This file is part of ottd-macos9 — a port of OpenTTD to Mac OS 9 / PowerPC.
# Copyright (c) 2026 Felipe De Bene. GPL v2 — see LICENSE/NOTICE.
"""Post-link gate for the xcofflink weak-static bug.

binutils' XCOFF linker can LOSE the allocation of a function-local static that
lives in a weak (inline-emitted) function: every weak copy of the datum is
discarded and the surviving references resolve into a NEIGHBOURING csect.  The
result is two live objects at one address — measured in R1-186 as
PoolBase::GetPools()::pools landing on top of economy.cpp's CMD_ERROR, which
made the first pool ctor after economy's clobber dereference garbage and killed
Classic silently (QEMU survived by unmapped-read luck).  Root-cause session:
2026-08-17, see memory pixalito-imac-g4.

This gate decodes every TOC entry whose symbol names a function-local static
(_ZZ*) or its guard (_ZGV*) and requires the pointer to land in a csect of the
SAME name.  Read-only statics merged into per-TU .rop_/.ro_ text blobs and
interior .P<offset> labels are benign and skipped.  Exit 1 on any hit: the fix
is to hoist the static to file scope or an out-of-line strong definition, NOT
to silence the gate.

Usage: xcoff_weak_static_audit.py <objdump-binary> <file.xcoff>
"""
import re
import struct
import subprocess
import sys
import bisect

OBJDUMP, XCOFF = sys.argv[1], sys.argv[2]

hdrs = subprocess.run([OBJDUMP, '-h', XCOFF], capture_output=True, text=True).stdout
m = re.search(r'\.data\s+([0-9a-f]+)\s+[0-9a-f]+\s+[0-9a-f]+\s+([0-9a-f]+)', hdrs)
if not m:
    sys.exit('audit: cannot find .data section header')
data_size, data_off = int(m.group(1), 16), int(m.group(2), 16)
m = re.search(r'\.bss\s+([0-9a-f]+)\s+([0-9a-f]+)', hdrs)
bss_end = (int(m.group(1), 16) + int(m.group(2), 16)) if m else data_size

sym_re = re.compile(r'\[\s*\d+\]\(sec\s+(-?\d+)\).*?\(scl\s+\d+\)\s*\(nx\s+\d+\)\s*0x([0-9a-f]+)\s+(\S+)')
aux_re = re.compile(r'AUX val\s+(\d+).*typ\s+(\d+)\s+algn\s+\d+\s+clss\s+(\d+)')

csects = []    # sized csects in data/bss space
tocents = []   # (addr, name) TOC entries
labels = {}    # addr -> set(names) in data/bss space

prev = None
proc = subprocess.Popen([OBJDUMP, '-t', XCOFF], stdout=subprocess.PIPE, text=True)
for ln in proc.stdout:
    ln = ln.strip()
    sm = sym_re.match(ln)
    if sm:
        prev = sm.groups()
        continue
    if prev:
        am = aux_re.search(ln)
        if am:
            sec, addr, name = int(prev[0]), int(prev[1], 16), prev[2]
            val, typ, clss = map(int, am.groups())
            if sec in (2, 3):
                labels.setdefault(addr, set()).add(name)
                if typ in (1, 3) and val > 0 and clss != 3:
                    csects.append((addr, val, name))
                if sec == 2 and clss == 3:
                    tocents.append((addr, name))
        prev = None
proc.wait()

csects.sort()
starts = [c[0] for c in csects]

def containing(addr):
    i = bisect.bisect_right(starts, addr) - 1
    if 0 <= i and addr < csects[i][0] + csects[i][1]:
        return csects[i]
    return None

f = open(XCOFF, 'rb')
bad = []
checked = 0
for addr, name in tocents:
    if not (name.startswith('_ZZ') or name.startswith('_ZGV')):
        continue
    if re.search(r'\.P\d+$', name):
        continue  # interior offset label; parent is checked separately
    f.seek(data_off + addr)
    tgt = struct.unpack('>I', f.read(4))[0]
    checked += 1
    if name in labels.get(tgt, ()):  # resolved to a symbol of its own name
        continue
    c = containing(tgt)
    if c and c[2] == name:
        continue
    if c and re.search(r'\.(rw|ro|bs)_$', c[2]):
        # Per-TU section blob (-Os merges a TU's small locals into one csect);
        # a static resolved into a blob is that TU's own storage. The deadly
        # case — landing on a NAMED variable of another TU — still fails.
        continue
    if c is None and tgt >= bss_end:
        # Past the whole data+bss extent: a .text address, i.e. a read-only
        # static merged into a per-TU text blob. Benign for const data.
        continue
    where = ('inside %s @0x%x+%d' % (c[2], c[0], c[1])) if c else ('bare 0x%x' % tgt)
    bad.append('  %s -> %s' % (name, where))

if bad:
    print('xcoff weak-static audit FAILED: %d function-local statics lost their allocation:' % len(bad))
    for b in bad:
        print(b)
    print('Fix: hoist each static to file scope / an out-of-line strong definition.')
    sys.exit(1)
print('xcoff weak-static audit: %d TOC entries checked, all allocated. OK' % checked)
