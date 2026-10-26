#!/usr/bin/env python3
"""Compile production logger unchanged; only unavailable host platform types/env stubbed."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='dxmt-idle-delivery-') as directory:
    work = Path(directory)
    (work/'unknwn.h').write_text('#pragma once\nusing WCHAR=wchar_t;\n#define __cdecl\n')
    (work/'fixture.cpp').write_text(r'''
#include "log/log.hpp"
#include "util_env.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
namespace dxmt {
Logger Logger::s_instance("native.log");
namespace env {
std::string getEnvVar(const char *name) { auto p=std::getenv(name); return p?p:""; }
std::string getExeBaseName() { return "fixture"; }
}
}
int main(int argc, char **) {
  using dxmt::Logger;
  assert(!Logger::diagnostic(nullptr));
  assert(!Logger::diagnostic(""));
  assert(!Logger::diagnostic("\n"));
  assert(!Logger::diagnostic("bad\nline"));
  assert(!Logger::diagnostic("bad\rline"));
  std::string big(1024,'x');
  assert(!Logger::diagnostic(big.c_str()));
  big.resize(1012);
  assert(!Logger::diagnostic(big.c_str()));
  if (argc > 1) {
    std::cerr.setstate(std::ios::badbit);
    assert(!Logger::diagnostic("UNAVAILABLE"));
    std::cerr.clear();
    return 0;
  }
  assert(Logger::diagnostic("ONE\n"));
  big.resize(1011);
  assert(Logger::diagnostic(big.c_str()));
  Logger::info("NORMAL_INFO_SENTINEL");
  Logger::err("NORMAL_ERROR_SENTINEL");
}
''')
    command = ['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-I'+str(work), '-I'+str(ROOT/'src/util'),
               str(work/'fixture.cpp'), str(ROOT/'src/util/log/log.cpp'),
               '-o', str(work/'fixture')]
    subprocess.run(command, check=True, timeout=60)
    env = os.environ.copy()
    env.update(DXMT_LOG_LEVEL='none', DXMT_LOG_PATH='none')
    result = subprocess.run([str(work/'fixture')], env=env, capture_output=True, check=True, timeout=10)
    lines = result.stderr.splitlines(keepends=True)
    assert result.stdout == b'' and len(lines)==2 and lines[0]==b'info:  ONE\n'
    assert len(lines[1])==1019
    result = subprocess.run([str(work/'fixture'), 'unavailable'], env=env, capture_output=True, check=True, timeout=10)
    assert not result.stdout and not result.stderr
    env['DXMT_LOG_PATH']=str(work)
    result = subprocess.run([str(work/'fixture')], env=env, capture_output=True, check=True, timeout=10)
    assert (work/'fixture_native.log').read_bytes()==result.stderr
print('PASS production logger fallback, failed-stream return, level independence, file config, single-line/input bounds')
