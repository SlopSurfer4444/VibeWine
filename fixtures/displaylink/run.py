#!/usr/bin/env python3
"""Compile the actual WineDisplayLink class with deterministic CV/main-queue seams."""
from pathlib import Path
import subprocess
import sys
here = Path(__file__).resolve().parent
source = Path(sys.argv[1]) if len(sys.argv) > 1 else here.parents[1] / 'source/dlls/winemac.drv/cocoa_window.m'
s = source.read_text()
a = s.index('@interface WineDisplayLink : NSObject')
b = s.index('\n@end', s.index('@implementation WineDisplayLink', a)) + len('\n@end')
(here / 'extracted.m').write_text((here / 'prefix.m').read_text() + '\n' + s[a:b] + '\n' + (here / 'tests.m').read_text())
subprocess.run(['clang', '-Wall', '-Wextra', '-Wno-unused-parameter', '-fblocks', '-fno-objc-arc', '-framework', 'Foundation', str(here / 'extracted.m'), '-o', str(here / 'fixture')], check=True)
subprocess.run([str(here / 'fixture')], check=True)
