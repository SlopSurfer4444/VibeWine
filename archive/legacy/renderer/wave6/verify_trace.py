"""Reproduce the native offline protocol/API/consumer acceptance checks."""
from pathlib import Path
import subprocess
r=Path(__file__).resolve().parent;c=r/'consumer'
for source,name in [('trace_controller.cpp','cs2trace'),('tests_protocol.cpp','tests_protocol'),('tests_generation.cpp','tests_generation')]:
 subprocess.run(['clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread',str(c/source),'-o',str(c/name)],check=True)
for name,receipt in [('tests_protocol','test-results-protocol.txt'),('tests_generation','test-results-generation.txt')]:
 p=subprocess.run([str(c/name)],capture_output=True,text=True,check=True);(c/receipt).write_text(p.stdout+p.stderr);print(p.stdout,end='')
p=subprocess.run(['python3',str(c/'tests_cli.py'),'--controller',str(c/'cs2trace'),'-v'],capture_output=True,text=True,check=True);(r/'test-results-cli.txt').write_text(p.stdout+p.stderr);print(p.stderr,end='')
subprocess.run(['python3',str(r/'test_api.py')],check=True)
p=subprocess.run(['python3','-m','unittest','discover','-s',str(c),'-p','test*analyze.py'],capture_output=True,text=True,check=True);(c/'test-results-analyze.txt').write_text(p.stdout+p.stderr);print(p.stderr,end='')
