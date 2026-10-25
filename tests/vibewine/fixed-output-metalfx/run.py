#!/usr/bin/env python3
"""Extract real swapchain methods into CPU mocks; no Wine, GPU, game or installation."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SWAPCHAIN = 'src/d3d11/d3d11_swapchain.cpp'
HELPER = 'src/d3d11/d3d11_spatial_geometry.hpp'


def block(text, marker):
    start = text.index(marker)
    opening = text.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def replace(text, before, after):
    if text.count(before) != 1:
        raise AssertionError(f'expected exactly one production marker: {before!r}')
    return text.replace(before, after)


def generate(source):
    resize = 'HRESULT ' + block(source, '  ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,')
    resize = resize.replace(' final ', ' ')
    layer = block(source, '  void ApplyLayerProps()')
    fixture = (HERE / 'fixture.cpp').read_text()
    fixture = replace(fixture, '/* RESIZE_METHOD */', resize)
    fixture = replace(fixture, '/* LAYER_METHOD */', layer)
    constructor = source[source.index('    native_view_ ='):source.index('    presenter = Rc(new Presenter')]
    fixture = replace(fixture, '/* CONFIGURE */', block(constructor, '    if constexpr ('))
    present_start = source.index('    if constexpr (EnableMetalFX) {\n      chunk->emitcc([')
    present_end = source.index('    { cs2trace::ReasonScope reason(cs2trace::CommitReason::Present,cmd_queue.traceSerial());', present_start)
    return replace(fixture, '/* PRESENT_BRANCH */', source[present_start:present_end])


def mutate(source, helper, kind):
    if kind == 'public-output-coupling':
        source = replace(source, '    next_desc.Width = render_width;',
                         '    if(fixed_output_){render_width=fixed_output_width_;render_height=fixed_output_height_;}\n    next_desc.Width = render_width;')
    elif kind == 'layer-render-coupling':
        source = replace(source, 'fixed_output_ ? output_geometry_.width : desc_.Width * scale_factor', 'desc_.Width * scale_factor')
    elif kind == 'early-publication':
        source = replace(source, '    if constexpr (EnableMetalFX) {\n      Com<D3D11ResourceCommon> next_upscaled;',
                         '    backbuffer_ = next_public_buffer.prvRef();\n    if constexpr (EnableMetalFX) {\n      Com<D3D11ResourceCommon> next_upscaled;')
    elif kind == 'nil-scaler-retained':
        source = replace(source, '          next_scaler = nullptr;', '          (void)next_scaler;')
    elif kind == 'stale-scaler-on-fallback':
        source = replace(source, '      metalfx_scaler = std::move(next_scaler);', '      if(next_scaler) metalfx_scaler = std::move(next_scaler);')
    elif kind == 'allow-aspect-mismatch':
        helper = replace(helper, 'uint64_t(render_width) * result.height == uint64_t(render_height) * result.width', 'true')
    elif kind == 'allow-equal':
        helper = replace(helper, '(render_width < result.width || render_height < result.height)', 'true')
    elif kind == 'allow-msaa':
        source = replace(source, 'next_desc.SampleDesc.Count == 1 &&', 'true &&')
    elif kind == 'allow-hdr':
        source = replace(source, 'colorspace_ == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);', 'true);')
    elif kind == 'allow-hdr-format':
        source = replace(source, 'next_output.upscale && supported_format &&', 'next_output.upscale && (supported_format || true) &&')
    elif kind == 'invalid-pair-accepted':
        helper = replace(helper, 'output_width <= 0 || output_height <= 0 ||', 'false ||')
    elif kind == 'ignore-legacy-factor':
        source = replace(source, 'fixed_output_ ? next_output.width : double(next_buffer_desc.Width) * scale_factor', 'fixed_output_ ? next_output.width : double(next_buffer_desc.Width)')
    elif kind == 'config-default-enabled':
        source = replace(source, 'getOption<bool>("d3d11.metalfxPerformanceUpscale", false)', 'getOption<bool>("d3d11.metalfxPerformanceUpscale", true)')
    elif kind == 'config-ignores-template':
        position = source.index('    if constexpr (EnableMetalFX) {')
        source = source[:position] + source[position:].replace('if constexpr (EnableMetalFX)', 'if constexpr (true)', 1)
    elif kind == 'config-ignores-compile-gate':
        source = replace(source, '#if defined(DXMT_FIXED_OUTPUT_METALFX) && DXMT_FIXED_OUTPUT_METALFX', '#if 1')
    elif kind == 'skip-fallback-present':
        source = replace(source, '          ctx.present(backbuffer, presenter, vsync_duration, state);', '          (void)backbuffer;')
    elif kind == 'read-current-input-after-resize':
        source = replace(source, 'this, vsync_duration, backbuffer = backbuffer_->texture(),', 'this, vsync_duration,')
        source = replace(source, '](ArgumentEncodingContext &ctx) mutable {\n        ScalerInfo scaler_info{};', '](ArgumentEncodingContext &ctx) mutable {\n        auto backbuffer=backbuffer_->texture();\n        ScalerInfo scaler_info{};')
    elif kind == 'ignore-present-hdr-transition':
        source = replace(source, 'upscaled = upscaled_backbuffer_ && (!fixed_output_ ||\n            colorspace_ == DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709)\n            ? upscaled_backbuffer_->texture() : Rc<Texture>{},', 'upscaled = upscaled_backbuffer_ ? upscaled_backbuffer_->texture() : Rc<Texture>{},')
    else:
        raise ValueError(kind)
    return source, helper


def compile_run(compiler, directory, source, helper, arch, feature=1, sanitizers=False):
    directory.mkdir()
    (directory / 'production_geometry.hpp').write_text(helper)
    (directory / 'fixture.cpp').write_text(generate(source))
    command = [compiler, '-std=c++17', '-O0', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', f'-DDXMT_FIXED_OUTPUT_METALFX={feature}']
    if sanitizers:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    if arch != 'native':
        command += ['-arch', arch]
    command += [str(directory / 'fixture.cpp'), '-o', str(directory / 'fixture')]
    built = subprocess.run(command, text=True, capture_output=True, timeout=60)
    if built.returncode:
        raise RuntimeError(f'fixture compilation failed:\n{built.stdout}{built.stderr}')
    run = subprocess.run([str(directory / 'fixture')], text=True, capture_output=True, timeout=20)
    return dict(returncode=run.returncode, output=(run.stdout + run.stderr).strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-dir', type=Path, default=REPO)
    parser.add_argument('--baseline-ref', help='read-only git show of the historical swapchain; current helper supplies the mock type')
    parser.add_argument('--baseline-source', type=Path)
    parser.add_argument('--arch', choices=('native', 'x86_64'), default='native')
    parser.add_argument('--skip-mutations', action='store_true')
    parser.add_argument('--sanitizers', action='store_true', help='enable AddressSanitizer and UndefinedBehaviorSanitizer')
    args = parser.parse_args()
    root = args.source_dir.resolve()
    source, helper = (root / SWAPCHAIN).read_text(), (root / HELPER).read_text()
    # Complement the extracted constructor configuration block with source
    # assertions for its opt-in scope and the unexecuted Presenter constructor.
    constructor = source[source.index('    if constexpr (EnableMetalFX) {'):source.index('    presenter = Rc(new Presenter')]
    gate = constructor.index('#if defined(DXMT_FIXED_OUTPUT_METALFX) && DXMT_FIXED_OUTPUT_METALFX')
    endgate = constructor.index('#endif', gate)
    for setting in ('d3d11.metalfxPerformanceUpscale', 'd3d11.metalfxOutputWidth', 'd3d11.metalfxOutputHeight'):
        assert setting in constructor[gate:endgate], setting
    assert 'getOption<bool>("d3d11.metalfxPerformanceUpscale", false)' in constructor[gate:endgate]
    assert 'fixed_output_ ? 1.0f : scale_factor' in source, 'native scale must remain independent in fixed mode'
    compiler = shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        raise RuntimeError('C++17 compiler unavailable')
    results = {}
    with tempfile.TemporaryDirectory(prefix='dxmt-fixed-output-') as temporary:
        temp = Path(temporary)
        results['current'] = compile_run(compiler, temp / 'current', source, helper, args.arch, sanitizers=args.sanitizers)
        if results['current']['returncode']:
            raise AssertionError(results['current']['output'])
        results['feature-disabled'] = compile_run(compiler, temp / 'disabled', source, helper, args.arch, feature=0, sanitizers=args.sanitizers)
        assert results['feature-disabled']['returncode'] == 0, results['feature-disabled']['output']
        baseline = None
        if args.baseline_ref:
            baseline = subprocess.check_output(['git', '-C', str(root), 'show', f'{args.baseline_ref}:{SWAPCHAIN}'], text=True, timeout=20)
        if args.baseline_source:
            assert baseline is None, 'choose only one baseline source'
            baseline = args.baseline_source.read_text()
        if baseline is not None:
            results['baseline'] = compile_run(compiler, temp / 'baseline', baseline, helper, args.arch, sanitizers=args.sanitizers)
            assert results['baseline']['returncode'] == 1 and results['baseline']['output'] == 'FAIL: fixed output texture dimensions', results['baseline']
        if not args.skip_mutations:
            kinds = ['public-output-coupling', 'layer-render-coupling', 'early-publication',
                     'nil-scaler-retained', 'stale-scaler-on-fallback', 'allow-aspect-mismatch',
                     'allow-equal', 'allow-msaa', 'allow-hdr', 'allow-hdr-format',
                     'invalid-pair-accepted', 'ignore-legacy-factor', 'config-default-enabled',
                     'config-ignores-template', 'config-ignores-compile-gate', 'skip-fallback-present',
                     'read-current-input-after-resize', 'ignore-present-hdr-transition']
            for kind in kinds:
                mutated_source, mutated_helper = mutate(source, helper, kind)
                feature = 0 if kind == 'config-ignores-compile-gate' else 1
                results[kind] = compile_run(compiler, temp / kind, mutated_source, mutated_helper, args.arch, feature=feature, sanitizers=args.sanitizers)
                output = results[kind]['output']
                assert results[kind]['returncode'] == 1 and output.startswith('FAIL: ') and '\n' not in output, f'undetected/non-behavioral mutation: {kind}: {results[kind]}'
    receipt = dict(scope='CPU-extracted production configuration, ResizeBuffers, ApplyLayerProps, and queued Present branch plus actual geometry helper; no native GPU/Wine/game',
                   arch=args.arch, sanitizers=args.sanitizers, compiler=compiler,
                   hashes={path: hashlib.sha256((root/path).read_bytes()).hexdigest() for path in (SWAPCHAIN, HELPER)},
                   results=results)
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
