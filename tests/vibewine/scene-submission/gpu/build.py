#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Build native Metal scheduling probes; never execute the resulting programs."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]


def extract(text, begin, end):
    if text.count(begin) != 1 or text.count(end) != 1:
        raise ValueError("source seam missing or ambiguous")
    return text[text.index(begin):text.index(end) + len(end)]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True, help="new absolute build directory")
    a = p.parse_args()
    if not a.output.is_absolute() or a.output.exists() or a.output.is_symlink():
        p.error("output must be a new absolute directory")
    source = ROOT / "src/d3d11/d3d11_swapchain.cpp"
    text = source.read_text()
    scheduling = extract(text, "    auto sync_state = SyncFrame(++presentation_count_);",
                         "    cmd_queue.PresentBoundary(presented_frame, max_latency);")
    admission = extract(text, "  class SyncFrameState {", "    return SyncFrameState();\n  };")
    if "device_context_->Flush();" not in scheduling or "sync_state = std::move(sync_state)" not in scheduling:
        raise ValueError("production source lacks scene-submission candidate")
    spec = importlib.util.spec_from_file_location("scene_cpu_builder", HERE.parent / "run.py")
    cpu = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(cpu)
    contents = cpu.sources(ROOT)
    if contents['src/d3d11/d3d11_swapchain.cpp'] != text:
        raise ValueError('production source changed during extraction; retry after owner quiesces')
    context_source = ROOT / 'src/dxmt/dxmt_context.cpp'
    context_text = context_source.read_text()
    marker = '// Missing drawable / pipeline failure still completes this encoder\'s'
    fallback_region = context_text[context_text.index(marker):context_text.index('      data->~PresentData();',context_text.index(marker))]
    fallback = extract(fallback_region,
                       '        auto encoder = cmdbuf.blitCommandEncoder();', '        encoder.endEncoding();')
    native = (HERE / 'fixture.mm.in').read_text().replace('/* PRODUCTION_NIL_FALLBACK */', fallback)
    generated = "#define SCENE_SUBMISSION_FIXTURE_NO_MAIN\n" + cpu.generate(contents) + native
    if len(generated.encode()) > 1024 * 1024:
        raise ValueError('generated source exceeds 1 MiB cap')
    spec = importlib.util.spec_from_file_location('bounded_gpu_builder', HERE.parents[1] / 'gpu-wave/build.py')
    bounded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bounded)
    a.output.mkdir(mode=0o700)
    fixture = a.output / "fixture.mm"
    fixture.write_text(generated)
    receipt = {"schema": 1, "runtimeExecuted": False,
               "builderSha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
               "productionSource": str(source), "productionSha256": hashlib.sha256(text.encode()).hexdigest(),
               "schedulingSha256": hashlib.sha256(scheduling.encode()).hexdigest(),
               "admissionSha256": hashlib.sha256(admission.encode()).hexdigest(), "builds": []}
    receipt['sourcesSha256'] = {p: hashlib.sha256(c.encode()).hexdigest() for p,c in contents.items()}
    receipt['sourcesSha256']['src/dxmt/dxmt_context.cpp'] = hashlib.sha256(context_text.encode()).hexdigest()
    receipt['generatedSha256'] = hashlib.sha256(generated.encode()).hexdigest()
    receipt['fallbackSha256'] = hashlib.sha256(fallback.encode()).hexdigest()
    receipt['adapterSha256'] = hashlib.sha256((HERE.parent/'fixture.cpp').read_bytes()).hexdigest()
    for name, split, mutation in (("reference", 0, ''), ("candidate", 1, ''),
                                  ("negative-no-flush", 1, 'flush'),
                                  ("negative-no-fallback-edge", 1, 'fallback')):
        build_source = fixture
        if mutation:
            build_source = a.output / (name+'.mm')
            if mutation == 'flush':
                mutant = generated.replace("device_context_->Flush();", "/* deliberately omitted Flush */")
            else:
                edge = '        data->fence_update.forEach([&](auto id) { encoder.updateFence(fence_pool_[id]); });'
                if generated.count(edge) != 1:
                    raise ValueError('fallback mutation seam ambiguous')
                mutant = generated.replace(edge,'        /* deliberately omitted fallback fence update */')
            build_source.write_text(mutant)
        command = ["xcrun", "clang++", "-std=c++20", "-O2", "-fobjc-arc", "-fblocks",
                   "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-unused-variable",
                   f"-DDXMT_SCENE_SUBMISSION={split}", str(build_source),
                   "-framework", "Foundation", "-framework", "Metal", "-o", str(a.output/name)]
        result = bounded.bounded_command(command,60)
        log = result.pop('output')
        (a.output/(name+".log")).write_bytes(log)
        item = {"name": name, "argv": command, **result, "negativeControl": mutation}
        if result['returncode'] == 0:
            binary = a.output/name
            if not binary.is_file() or binary.stat().st_size > 16 * 1024 * 1024:
                item['returncode'] = result['returncode'] = 1
                item['error'] = 'missing binary or binary exceeds 16 MiB cap'
            else:
                item["sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
        receipt["builds"].append(item)
        if result['returncode'] != 0 or result['timedOut'] or result['outputTruncated']:
            break
    encoded = json.dumps(receipt, indent=2)+"\n"
    if len(encoded.encode()) > 32 * 1024:
        raise ValueError('receipt exceeds 32 KiB cap')
    (a.output/"build.json").write_text(encoded)
    accepted = len(receipt['builds']) == 4 and all(x['returncode'] == 0 and not x['timedOut'] and not x['outputTruncated'] for x in receipt['builds'])
    print(json.dumps({"status": "compiled_only" if accepted else "failed",
                      "receipt": str(a.output/"build.json")}))
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
