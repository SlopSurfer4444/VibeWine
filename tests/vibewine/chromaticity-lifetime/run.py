#!/usr/bin/env python3
"""Actual production tag reader, real CFData ownership, no display or game."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref')
args = parser.parse_args()
rel = 'src/winemetal/unix/winemetal_unix.c'
text = (subprocess.check_output(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':' + rel]).decode()
        if args.baseline_ref else (ROOT / rel).read_text())
start = text.index('typedef struct icc_XYZ_t {')
end = text.index('\nbool\nGetDisplayColorGamut(', start)
production = text[start:end]
preamble = r'''
#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
typedef CFTypeRef ColorSyncProfileRef;
static CFDataRef current;
static unsigned copies;
static CFDataRef ColorSyncProfileCopyTag(ColorSyncProfileRef p, CFStringRef tag) {
  (void)p; (void)tag; copies++;
  return current ? (CFDataRef)CFRetain(current) : NULL;
}
'''
body = r'''
static unsigned cases, leaks, failures;
static void check(bool ok, const char *name) {
  if (!ok) { fprintf(stderr, "FAIL %s\n", name); failures++; }
}
static void run(const char *name, const uint8_t *data, CFIndex size, bool present,
                bool expected, float x, float y) {
  current = present ? CFDataCreate(kCFAllocatorDefault, data, size) : NULL;
  CFIndex before = current ? CFGetRetainCount(current) : 0;
  copies = 0;
  for (unsigned i=0; i<128; ++i) {
    float actual_x=91, actual_y=92;
    bool ok=GetChromaticity_xy(NULL, CFSTR("fixture"), &actual_x, &actual_y);
    check(ok==expected, name);
    if (!expected) check(actual_x==91 && actual_y==92, "failure preserves output");
    else if (isnan(x)) check(isnan(actual_x) && isnan(actual_y), "zero-sum semantics retained");
    else check(fabsf(actual_x-x)<0.00001f && fabsf(actual_y-y)<0.00001f, "unchanged chromaticity");
  }
  check(copies==128, "all copy calls exercised");
  if (current) {
    CFIndex after=CFGetRetainCount(current);
    if (after!=before) {
      fprintf(stderr,"LEAK %s retained=%ld\n",name,(long)(after-before));
      leaks++;
      /* Retire test-owned leaked references after observing the failure. */
      while (after-- > before) CFRelease(current);
    }
    CFRelease(current); current=NULL;
  }
  cases++;
}
int main(void) {
  icc_XYZ_t good={0x205a5958,0,(int32_t)__builtin_bswap32(65536),
      (int32_t)__builtin_bswap32(131072),(int32_t)__builtin_bswap32(196608)};
  icc_XYZ_t wrong=good; wrong.sig=0;
  icc_XYZ_t zero={0x205a5958,0,0,0,0};
  uint8_t oversized[sizeof(good)+1]={0};
  run("absent",NULL,0,false,false,0,0);
  run("empty",NULL,0,true,false,0,0);
  run("short",(uint8_t *)&good,sizeof(good)-1,true,false,0,0);
  run("long",oversized,sizeof(oversized),true,false,0,0);
  run("signature",(uint8_t *)&wrong,sizeof(wrong),true,false,0,0);
  run("valid",(uint8_t *)&good,sizeof(good),true,true,1.0f/6,1.0f/3);
  run("zero",(uint8_t *)&zero,sizeof(zero),true,true,NAN,NAN);
  printf("cases=%u copies=%u semantic_failures=%u leaking_paths=%u\n",cases,cases*128,failures,leaks);
  return failures ? 2 : leaks ? 17 : 0;
}
'''
with tempfile.TemporaryDirectory(prefix='vibewine-chromaticity-') as directory:
    work = Path(directory)
    unit, exe = work / 'fixture.m', work / 'fixture'
    unit.write_text(preamble + production + body)
    subprocess.run([os.environ.get('CC', 'clang'), '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(unit), '-framework', 'CoreFoundation',
                    '-o', str(exe)], check=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15,
                            env={**os.environ, 'UBSAN_OPTIONS': 'halt_on_error=1'})
    print(result.stdout, end='')
    print(result.stderr, end='')
    if args.baseline_ref:
        expected = ['LEAK ' + name + ' retained=128' for name in ('empty','short','long','signature','valid','zero')]
        assert result.returncode == 17 and result.stderr.splitlines() == expected, result
        assert 'semantic_failures=0 leaking_paths=6' in result.stdout
        print('PASS exact baseline ownership failures; output semantics preserved')
    else:
        assert result.returncode == 0 and not result.stderr, result
        assert 'semantic_failures=0 leaking_paths=0' in result.stdout
        print('PASS real CFData copy ownership on every acquired exit')
