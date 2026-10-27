#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Complete production packed-list template under ASan/UBSan; no GPU."""
import argparse,hashlib,json,os,platform,subprocess,tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--arch',choices=['arm64','x86_64']);a=p.parse_args()
here=Path(__file__).resolve().parent;source=here.parents[2];header=(source/'src/airconv/packed_variant_list.hpp').read_text()
header=header.replace('public:\n  using variant','public:\n  struct TestState { const void *data; size_t capacity,used; };\n  TestState test_state() const { return {data,capacity,size}; }\n  void test_set_size(size_t n) { size=n; }\n  void test_set_capacity(size_t n) { capacity=n; }\n  using variant')
header=header.replace('std::malloc(','test_malloc(').replace('std::free(','test_free(').replace('std::realloc(','test_realloc(')
with tempfile.TemporaryDirectory(prefix='dxmt-ir-compact-unit-') as td:
 work=Path(td);(work/'packed_variant_list.hpp').write_text(header)
 for growth in [0,1]:
  exe=work/('fixture'+str(growth));cmd=['clang++','-std=c++20','-O1','-gline-tables-only','-fno-exceptions','-fno-rtti','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DDXMT_PACKED_IR_GROWTH='+str(growth),'-I',str(work),str(here/'fixture.cpp'),'-o',str(exe)]
  if a.arch:cmd+=['-arch',a.arch]
  subprocess.run(cmd,check=True,timeout=60)
  launch=[str(exe)]
  if a.arch and platform.machine()!=a.arch:launch=['arch','-'+a.arch,*launch]
  r=subprocess.run(launch,capture_output=True,text=True,timeout=20);assert r.returncode==0,r.stdout+r.stderr
  print(json.dumps({'arch':a.arch or platform.machine(),'growth':growth,'output':r.stdout.strip(),'header_sha256':hashlib.sha256((source/'src/airconv/packed_variant_list.hpp').read_bytes()).hexdigest()}))
 assert sum(x.stat().st_size for x in work.iterdir())<32*1024*1024
