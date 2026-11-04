#!/usr/bin/env python3
"""Current focused successor checks; older aggregate is retained as historical source."""
from pathlib import Path
import runpy
runpy.run_path(str(Path(__file__).resolve().parents[1]/'renderer/wave7/next-tests/run.py'),run_name='__main__')
