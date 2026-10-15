#!/usr/bin/env python3
"""Compile the procedural virtual-mode PE only; never launch it."""
from pathlib import Path
import os,subprocess
root=Path(__file__).resolve().parent
cc=Path(os.environ['LLVM_MINGW'])/'x86_64-w64-mingw32-clang++'
subprocess.run([str(cc),'-std=c++17','-D_WIN32_WINNT=0xa00','-O2','-Wall','-Wextra','-Werror','-static',str(root/'guest.cpp'),'-o',str(root/'guest.exe'),'-ld3d11','-ldxgi','-ldxguid','-ld3dcompiler','-luser32','-ladvapi32'],check=True)
