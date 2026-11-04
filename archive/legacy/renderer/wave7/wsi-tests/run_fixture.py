#!/usr/bin/env python3
"""Compile actual candidate WSI and selected swapchain methods with Win32 doubles.

No production function body is reproduced by this runner. The generated TU embeds
the candidate bytes after preprocessing only local includes, and reports SHA256
identities for the tested source. This is a portable control-flow fixture, not a
Wine/Win32 ABI, compositor, or real fullscreen validation.
"""
import argparse
import hashlib
import pathlib
import re
import subprocess
import tempfile

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent / "source"


def strip_includes(text):
    return re.sub(r'^\s*#\s*(?:include|pragma once).*$', '', text, flags=re.M)


def balanced(text, start):
    """Use brace balance for these production bodies; strings contain no braces."""
    opening = text.index('{', start)
    level = 1
    end = opening + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def method(text, name):
    match = re.search(r'\b(?:HRESULT|void|bool|double)\s+(?:STDMETHODCALLTYPE\s+)?' + name + r'\(', text)
    if match is None:
        raise RuntimeError('Cannot find production method ' + name)
    start = match.start()
    return balanced(text, start).replace(' final {', ' {')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=pathlib.Path, default=ROOT)
    parser.add_argument('--baseline-ref', help='use these Git source bodies and require the HWND-reuse regression to fail')
    parser.add_argument('--mutation-check', action='store_true',
                        help='prove connected tests reject deliberately regressed production bodies')
    args = parser.parse_args()
    files = [args.source / 'src/util/wsi_platform_win32.hpp',
             args.source / 'src/util/wsi_window_win32.cpp']
    files.append(args.source / 'src/d3d11/d3d11_swapchain.cpp')
    contents = [p.read_text() for p in files]
    if args.baseline_ref:
        if args.mutation_check:
            parser.error('--baseline-ref and --mutation-check are separate checks')
        contents = [subprocess.run(['git', 'show', args.baseline_ref + ':' + str(p.relative_to(args.source))],
                                   cwd=args.source, text=True, capture_output=True, check=True).stdout for p in files]
    state, wsi, swapchain = contents
    for path, content in zip(files, [state, wsi, swapchain]):
        print(f'SOURCE {hashlib.sha256(content.encode()).hexdigest()} {path}', flush=True)
    feedback = (args.source / 'src/winemetal/wave7_feedback.h').read_text()
    unit = '#include <cstdint>\n#define __stdcall\n' + strip_includes(feedback) + '\n' + (HERE / 'win32_doubles.hpp').read_text() + '\n' + strip_includes(state)
    if 'HANDLE capture_token' in state:
        allocator_source = args.source / 'src/dxgi/dxgi.cpp'
        allocator_text = allocator_source.read_text()
        allocator_start = allocator_text.index('static std::atomic<ULONG_PTR> nextFullscreenToken')
        allocator_body = balanced(allocator_text, allocator_text.index('extern "C" HANDLE __stdcall DXMTAllocateFullscreenToken()'))
        allocator_declaration = allocator_text[allocator_start:allocator_text.index('\n', allocator_start)]
        unit += '\n' + allocator_declaration + '\n' + allocator_body + '\n'
        print(f'SOURCE {hashlib.sha256(allocator_text.encode()).hexdigest()} {allocator_source}', flush=True)
    unit += '\n#define WSI_CAPTURE_GENERATIONS ' + ('1' if 'HANDLE capture_token' in state else '0') + '\n'
    unit += '\n' + strip_includes(wsi)
    if 'HANDLE capture_token' in state:
        # Two independently compiled WSI capture bodies, one actual DXGI owner.
        # Only pure helper dependencies are shared with the first WSI copy.
        unit += '\nnamespace dxmt::wsi_peer {\n'
        for name in ['DXMTWindowState', 'getDesktopCoordinates', 'getWindowLongChecked',
                     'setWindowLongChecked', 'fullscreenStateProperty', 'ownsFullscreenState']:
            unit += 'using dxmt::wsi::' + name + ';\n'
        unit += balanced(wsi, wsi.index('bool enterFullscreenMode(')) + '\n}\n'
    unit += '\n' + balanced(swapchain, swapchain.index('class ModeSetGuard')) + ';\n'
    unit += (HERE / 'swapchain_doubles.hpp').read_text()
    for name in ['SetFullscreenState', 'EnterFullscreenMode', 'LeaveFullscreenMode',
                 'ChangeDisplayMode', 'RestoreDisplayMode', 'ResizeTarget',
                 'CleanupFullscreenForDestruction', 'RefreshDisplayTiming', 'NeedsDisplayTimingRefresh', 'PresentationDuration']:
        extracted = method(swapchain, name)
        if name == 'ResizeTarget':
            # Keep the production signed LONG vs unsigned UINT comparison intact.
            # Production builds do not enable -Werror; suppress this warning only
            # around the extracted method, retaining all other fixture warnings.
            unit += '\n#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Wsign-compare"\n'
            unit += extracted
            unit += '\n#pragma clang diagnostic pop\n'
        else:
            unit += '\n' + extracted
    # Extract constructor tail unchanged, including initialization result flow.
    start = swapchain.index('    initialization_result_ = ResizeBuffers(')
    end = swapchain.index('\n  };', start)
    unit += '\nvoid InitializeFromConstructorTail() {\n' + swapchain[start:end] + '\n}\n'
    unit += '\n};\n' + (HERE / 'wsi_cases.cpp').read_text()
    with tempfile.TemporaryDirectory(prefix='wave7-wsi-') as tmp:
        source = pathlib.Path(tmp) / 'fixture.cpp'
        binary = pathlib.Path(tmp) / 'fixture'
        source.write_text(unit)
        command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                   '-Wno-unused-parameter', '-fsanitize=address,undefined',
                   '-fno-omit-frame-pointer', '-g', str(source), '-o', str(binary)]
        subprocess.run(command, check=True)
        if args.baseline_ref:
            result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=10)
            expected = 'FAIL fullscreen update accepted a recycled HWND'
            if result.returncode == 0 or expected not in result.stderr:
                raise RuntimeError('baseline did not reach expected HWND-reuse failure: ' + result.stdout + result.stderr)
            print('BASELINE REJECTED ' + args.baseline_ref + ': ' + result.stderr.strip(), flush=True)
            return
        subprocess.run([str(binary)], check=True, timeout=10)
        if args.mutation_check:
            mutations = [
                ('capture receipt not required for equal mode',
                 'if (displayCaptureCleanupComplete())', 'if (true)'),
                ('capture receipt ignored after restore attempt',
                 'return ret == DISP_CHANGE_SUCCESSFUL && displayCaptureCleanupComplete();',
                 'return ret == DISP_CHANGE_SUCCESSFUL;'),
                ('constructor entry failure status ignored',
                 'initialization_result_ = EnterFullscreenMode(nullptr);',
                 'EnterFullscreenMode(nullptr);'),
                ('fullscreen move failure ignored',
                 'return positioned && ownsWindow();' if 'return positioned && ownsWindow();' in unit else 'return positioned && ::IsWindow(hWindow);',
                 '(void)positioned; return ownsWindow();' if 'return positioned && ownsWindow();' in unit else '(void)positioned; return ::IsWindow(hWindow);'),
                ('restored display cadence discarded',
                 'RefreshDisplayTiming(wsi::getWindowMonitor(hWnd));',
                 'display_refresh_rate_ = DBL_MAX;'),
                ('unchecked SetWindowLong error',
                 'return previous != 0 || ::GetLastError() == ERROR_SUCCESS;',
                 '(void)previous; return true;'),
                ('zero GetWindowLong success rejected',
                 'return *value != 0 || ::GetLastError() == ERROR_SUCCESS;',
                 'return *value != 0;'),
                ('recycled HWND property check omitted',
                 '::GetPropW(window, fullscreenStateProperty) == token' if 'HANDLE capture_token' in state else '::GetPropW(window, fullscreenStateProperty) == reinterpret_cast<HANDLE>(const_cast<DXMTWindowState *>(state))',
                 'true'),
                ('current DEVMODE size omitted',
                 'current_mode.dmSize = sizeof(current_mode);', ''),
                ('registry DEVMODE size omitted',
                 'registry_mode.dmSize = sizeof(registry_mode);', ''),
                ('logical windowed exit skips retry',
                 'else if (!Fullscreen)',
                 'else if (!fullscreen_desc_.Windowed && !Fullscreen)'),
                ('ResizeTarget prematurely commits descriptor',
                 'HRESULT hr = ChangeDisplayMode(target_.ptr(), &newDisplayMode);',
                 'fullscreen_desc_.RefreshRate = newDisplayMode.RefreshRate;\n'
                 '      HRESULT hr = ChangeDisplayMode(target_.ptr(), &newDisplayMode);'),
                ('terminal ownership detach omitted',
                 '!wsi::detachFullscreenState(hWnd, &window_state_)',
                 'false'),
            ]
            if 'return positioned && ownsWindow();' in unit:
                mutations.append(('fullscreen update HWND identity ignored',
                                  '::GetPropW(hWindow, fullscreenStateProperty) == owner;',
                                  'true;'))
            if 'HANDLE capture_token' in state:
                mutations.extend([
                    ('state address reused as capture token',
                     'const HANDLE token = DXMTAllocateFullscreenToken();',
                     'const HANDLE token = reinterpret_cast<HANDLE>(pState);'),
                    ('capture token allocator wraps',
                     'while (value != ~ULONG_PTR(0)) {', 'while (true) {'),
                    ('stale leave clears recaptured state',
                     'if (state->capture_token == token)\n    *state = {};', '*state = {};')])
            for name, old, new in mutations:
                expected_count = 2 if name == 'state address reused as capture token' else 1
                if unit.count(old) != expected_count:
                    raise RuntimeError('mutation seam changed: ' + name)
                source.write_text(unit.replace(old, new, expected_count))
                subprocess.run(command, check=True, capture_output=True)
                result = subprocess.run([str(binary)], text=True, capture_output=True)
                if result.returncode == 0 or 'FAIL ' not in result.stderr:
                    raise RuntimeError('mutation was not behaviorally rejected: ' + name)
                print(f'MUTATION REJECTED {name}: {result.stderr.strip()}', flush=True)


if __name__ == '__main__':
    main()
