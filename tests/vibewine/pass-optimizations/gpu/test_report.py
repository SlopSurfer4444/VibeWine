#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Offline report acceptance/rejection, no Wine or GPU."""
from analyze import MODULES, validate

modules = ''.join(f'MODULE {name} Z:\\private\\{name}\n' for name in MODULES)
normal = modules + ''.join(f'CASE_PASS mode={i}\n' for i in range(4)) + 'PASS cases=4 images=5\n'
negative = modules + 'NEGATIVE_REJECTED pixel\n'
assert validate(normal.encode(), 0)['images'] == 5
assert validate(negative.encode(), 20, True)['status'] == 'negative_rejected'
rejected = [
    (normal.encode(), 1, False), (normal.encode(), 124, False),
    (normal.replace('CASE_PASS mode=2\n', '').encode(), 0, False),
    (normal.replace('CASE_PASS mode=2', 'CASE_PASS mode=1').encode(), 0, False),
    ((normal + 'FAIL later\n').encode(), 0, False),
    (normal.replace('PASS cases=4 images=5', 'PASS cases=4 images=4').encode(), 0, False),
    (negative.encode(), 0, True), (negative.encode(), 1, True),
    ((modules + 'FAIL image_all_pixels_exact\n').encode(), 1, True),
    (negative.encode(), 20, False),
    (normal.replace('MODULE dxgi.dll', 'MODULE d3d11.dll').encode(), 0, False),
    (b'x' * (64 * 1024 + 1), 0, False), (b'\xff', 0, False),
]
for data, code, control in rejected:
    try:
        validate(data, code, control)
    except (ValueError, UnicodeError):
        continue
    raise AssertionError((code, control, data[:100]))
print(f'PASS accepted=2 rejected={len(rejected)} CPU report checks only')
