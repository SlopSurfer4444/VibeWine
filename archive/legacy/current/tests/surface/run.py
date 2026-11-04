#!/usr/bin/env python3
"""Compile exact surface functions and exercise native NSView composition.

All generated sources, executables and reports live in an automatic temporary
directory. This runner needs macOS, Clang++, AppKit and the system patch utility.
"""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
CURRENT = HERE.parents[1]
FILES = ('window.c', 'cocoa_window.m', 'd3dmetal.c', 'macdrv.h', 'macdrv_cocoa.h')
NEGATIVE_CASES = (
    'd3dmetal bridge calloc failure', 'd3dmetal array allocation failure',
    'client allocation failure', 'native allocation failure',
    'd3dmetal caller allocation failure', 'd3dmetal caller native failure',
    'child composition retina=0 parent=1 dpi=144',
    'child composition retina=1 parent=1 dpi=171',
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(path, token):
    source = path.read_text()
    start = source.index(token)
    begin = source.index('{', start)
    end, depth = begin + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def generate(tree):
    fixture = (HERE / 'fixture.mm').read_text()
    fixture = fixture.replace('// DESTROY', 'constexpr int GA_ROOT=2,MDT_RAW_DPI=2;\n// DESTROY')
    fixture = fixture.replace('constexpr int GA_ROOT=2,MDT_RAW_DPI=2;\n// CREATE', '// CREATE')
    for marker, file, token in (
        ('RECT_CONVERSION', 'macdrv.h', 'static inline CGRect cgrect_from_rect('),
        ('RETINA_CONVERSION', 'macdrv_cocoa.h', 'static inline CGRect cgrect_mac_from_win('),
        ('NATIVE_CREATE', 'cocoa_window.m', 'macdrv_view macdrv_create_view('),
        ('DESTROY', 'window.c', 'static void macdrv_client_surface_destroy('),
        ('DETACH', 'window.c', 'static void macdrv_client_surface_detach('),
        ('UPDATE', 'window.c', 'static void macdrv_client_surface_update('),
        ('PRESENT', 'window.c', 'static void macdrv_client_surface_present('),
        ('CREATE', 'window.c', 'struct macdrv_client_surface *macdrv_client_surface_create('),
    ):
        source = extract(tree / file, token)
        if marker == 'NATIVE_CREATE':
            source = source.replace('[NSNotificationCenter defaultCenter]', 'fixture_center')
        if marker == 'CREATE':
            source = source.replace('= client_surface_create(', '= (macdrv_client_surface *)client_surface_create(')
        fixture = fixture.replace('// ' + marker, source)
    for marker, token in (
        ('CORE_CREATE', 'void *client_surface_create('),
        ('CORE_RELEASE', 'void client_surface_release('),
    ):
        source = extract(HERE / 'context/win32u_client_surface.c', token)
        source = source.replace('= calloc(', '= (struct client_surface *)calloc(')
        fixture = fixture.replace('// ' + marker, source)
    caller = 'using COLORREF=unsigned;using HANDLE=void*;\n'
    caller += extract(tree / 'd3dmetal.c', 'struct d3dmetal_macdrv_win_data\n{') + ';\n'
    caller += 'static void cf_client_surface_release(CFAllocatorRef,const void*v){client_surface_release((client_surface*)v); }\n'
    caller += 'static void macdrv_set_view_d3dmetal_client_surface(void*,client_surface*){}\n'
    caller += (extract(tree / 'd3dmetal.c', 'static struct d3dmetal_macdrv_win_data *my_get_win_data(')
               .replace('= calloc(', '= (struct d3dmetal_macdrv_win_data *)bridge_calloc(')
               .replace('free(d3dm_data)', 'bridge_free(d3dm_data)')
               .replace('CFArrayCreateMutable(', 'bridge_array_create(')
               .replace('CFRelease(', 'bridge_array_release('))
    return fixture.replace('// D3DMETAL_CALLER', caller)


def run(executable, *arguments):
    result = subprocess.run([str(executable), *arguments], capture_output=True, text=True, timeout=25)
    return {'exitCode': result.returncode, 'stdout': result.stdout, 'stderr': result.stderr}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=CURRENT / 'winemac')
    args = parser.parse_args()
    source = args.source.resolve()
    results = {}
    with tempfile.TemporaryDirectory(prefix='wine-surface-fixture-') as temporary:
        work = Path(temporary)
        donor = work / 'donor/winemac.drv'
        donor.mkdir(parents=True)
        for file in FILES:
            shutil.copyfile(source / file, donor / file)
        # The reverse patch reconstructs the immediate public predecessor,
        # including its earlier top-level raw-extent fix, without requiring Git.
        subprocess.run(['patch', '-R', '-p1', '-f', '-i', str(CURRENT / 'surface-followup.patch')],
                       cwd=donor.parent, check=True, capture_output=True, text=True)
        for label, tree in (('candidate', source), ('donor-negative-control', donor)):
            generated = work / (label + '.mm')
            generated.write_text(generate(tree))
            executable = work / (label + '-test')
            command = ['/usr/bin/clang++', '-std=c++20', '-fblocks', '-g',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                       '-Wno-deprecated-declarations', '-Wno-incompatible-pointer-types',
                       '-I' + str(HERE / 'context'), str(generated), '-framework', 'AppKit',
                       '-o', str(executable)]
            build = subprocess.run(command, capture_output=True, text=True)
            if build.returncode:
                raise RuntimeError(label + ' compile failed:\n' + build.stdout + build.stderr)
            results[label] = run(executable)
            results[label]['sourceHashes'] = {file: sha(tree / file) for file in FILES}
            results[label]['generatedSourceSha256'] = sha(generated)
        results['isolatedNegativeControls'] = {
            name: run(work / 'donor-negative-control-test', name) for name in NEGATIVE_CASES
        }
    results['fixtureSha256'] = sha(HERE / 'fixture.mm')
    results['runnerSha256'] = sha(Path(__file__))
    results['coreContextSha256'] = sha(HERE / 'context/win32u_client_surface.c')
    results['passed'] = (
        results['candidate']['exitCode'] == 0
        and results['donor-negative-control']['exitCode'] != 0
        and all(control['exitCode'] != 0 for control in results['isolatedNegativeControls'].values())
    )
    results.update(wineStarted=False, fullBuild=False, installedChanges=False)
    print(json.dumps(results, indent=2))
    return 0 if results['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
