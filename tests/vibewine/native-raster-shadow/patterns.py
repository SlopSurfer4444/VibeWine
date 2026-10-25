#!/usr/bin/env python3
"""Measure controlled production emission/optimizer patterns, never gameplay frequency."""
from pathlib import Path
import importlib.util, subprocess, tempfile, json, hashlib
ROOT=Path(__file__).resolve().parents[3]
def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

def main():
    emit=load(ROOT/'tests/vibewine/render-pass-fusion/run.py','emit')
    optimize=load(ROOT/'tests/vibewine/pass-optimizations/run.py','optimize')
    generated=emit.generate(*[(ROOT/p).read_text() for p in ('src/dxmt/dxmt_context.cpp','src/winemetal/winemetal_thunks.c','src/dxmt/dxmt_pass_optimizations.hpp')])
    generated=generated.replace('static unsigned crossings;','static unsigned crossings;\nstatic unsigned run_hist[100]{};')
    marker='  unsigned visited = 0;'
    injected='''  for(auto *scan=node;scan;) {
    if(scan->type!=WMTRenderCommandUseResource) {scan=(const wmtcmd_base *)scan->next.ptr;continue;}
    auto *first=(const wmtcmd_render_useresource *)scan;unsigned count=0;
    do {++count;scan=(const wmtcmd_base *)scan->next.ptr;if(!scan||scan->type!=WMTRenderCommandUseResource)break;
        auto *candidate=(const wmtcmd_render_useresource *)scan;
        if(candidate->usage!=first->usage||candidate->stages!=first->stages)break;
    }while(true);
    assert(count<100);++run_hist[count];
  }
'''
    assert generated.count(marker)==1;generated=generated.replace(marker,injected+marker)
    generated=generated.rstrip();assert generated.endswith('}')
    generated=generated[:-1]+'''for(unsigned i=1;i<100;++i)if(run_hist[i])std::cerr<<i<<" "<<run_hist[i]<<"\\n";\n}\n'''
    includes=['-I'+str(ROOT/p) for p in ('include','src/winemetal','src/airconv','src/dxmt','src/util')]
    common=['xcrun','clang++','-std=c++20','-O1','-Wall','-Wextra','-Werror','-Wno-pragma-once-outside-header','-fsanitize=address,undefined','-fno-sanitize-recover=all',*includes]
    stats={}
    with tempfile.TemporaryDirectory(prefix='dxmt-native-patterns-') as temp:
        temp=Path(temp)
        for dedup in (0,1):
            source=temp/f'emit{dedup}.cpp';binary=temp/f'emit{dedup}';source.write_text(generated)
            subprocess.run(common+[f'-DDXMT_PHYSICAL_RESIDENCY_DEDUP={dedup}','-DDXMT_RENDER_PASS_FUSION=1',str(source),'-o',str(binary)],check=True,timeout=60)
            result=subprocess.run([str(binary)],check=True,capture_output=True,text=True,timeout=20)
            hist={int(v[0]):int(v[1]) for line in result.stderr.splitlines() if(v:=line.split())}
            stats[str(dedup)]=dict(scenarios=len(result.stdout.splitlines()),same_mask_contiguous_runs=hist,declarations=sum(n*c for n,c in hist.items()),hypothetical_calls_batch32=sum(((n+31)//32)*c for n,c in hist.items()))
        fixture=optimize.generate(optimize.sources(ROOT));marker='int main(){dead_store_cases();residency_cases();'
        case='''static void clear_patterns() {
  for(unsigned mode=0;mode<3;++mode) {
    Surface s;Arena arena;ArgumentEncodingContext ctx;auto *c0=clear(arena,s);auto *c1=clear(arena,s);
    c0->id=1;c0->fence_update={1};c1->id=2;c1->fence_wait={1};c1->fence_update={2};c1->color={0.7,0.6,0.5,1};
    std::vector<EncoderData *> nodes{c0,c1};c0->next=c1;
    if(mode==1){auto *r=render(arena,s);c1->next=r;nodes.push_back(r);}
    if(mode==2){auto *event=arena.make<EncoderData>(EncoderType::SampleTimestamp);c0->next=event;event->next=c1;nodes={c0,event,c1};}
    ctx.optimize(nodes);unsigned count=0;for(auto *p:nodes)count+=p->type==EncoderType::Clear;
    require(count==(mode==1?0:2),"remaining clear pattern");
    if(mode==1)require(static_cast<RenderEncoderData *>(nodes.back())->colors[0].clear_color.r==0.7,"latest clear wins");
    if(mode==0)require(c1->fence_wait.contains(1),"later clear depends on earlier signal; naive deletion stalls");
    std::cout<<"CLEAR "<<mode<<" "<<count<<"\\n";
  }
}
'''
        # FenceSet's exact public API is intersectedWith, not a fixture-only method.
        case=case.replace('c1->fence_wait.contains(1)','c1->fence_wait.intersectedWith(c0->fence_update)')
        assert fixture.count(marker)==1;fixture=fixture.replace(marker,case+'int main(){clear_patterns();dead_store_cases();residency_cases();')
        source=temp/'clears.cpp';binary=temp/'clears';source.write_text(fixture)
        subprocess.run(common+['-DDXMT_PHYSICAL_RESIDENCY_DEDUP=1','-DDXMT_DEAD_ATTACHMENT_STORES=1',str(source),'-o',str(binary)],check=True,timeout=60)
        clear_output=subprocess.check_output([str(binary)],text=True,timeout=20)
    print(json.dumps(dict(scope='controlled actual production emission/optimizer with CPU adapters and ASan/UBSan; not game command frequency',render_emission=stats,clear_patterns=clear_output.splitlines(),source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in ('src/dxmt/dxmt_context.cpp','src/dxmt/dxmt_pass_optimizations.hpp')},limit='Existing 256-scenario emitter fixture has two duplicate declarations; this does not establish real-game run histogram.'),indent=2))
if __name__=='__main__':main()
