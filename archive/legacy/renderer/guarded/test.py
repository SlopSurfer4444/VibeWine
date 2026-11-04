from pathlib import Path
import subprocess,hashlib,json,difflib
root=Path(__file__).resolve().parent
old=root/'baseline/d3d11_swapchain.cpp'
new=root/'source/src/d3d11/d3d11_swapchain.cpp'
def block(s,start):
 p=s.index(start);a=s.index('{',p);level=1;i=a+1
 while level:
  level+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[p:i]
for label,path in [('baseline',old),('candidate',new)]:
 s=path.read_text(); guard=block(s,'class ModeSetGuard')+';\n'
 methods=block(s,'  HRESULT LeaveFullscreenMode()')+'\nHRESULT '+block(s,'  ResizeTarget(const DXGI_MODE_DESC *pDesc) final')
 text=(root/'fixture-prefix.cpp').read_text()+guard+'struct Test:Base {\nModeSetGuard modeset_guard_; dxmt::mutex mutex_; int monitor_=1,hWnd=1,window_state_=0; struct {bool Windowed=true; Ratio RefreshRate; int ScanlineOrdering=0,Scaling=0;} fullscreen_desc_; Target target_; Presenter p; Presenter* presenter=&p; HRESULT RestoreDisplayMode(int){return S_OK;} HRESULT ChangeDisplayMode(void*,DXGI_MODE_DESC1*){return S_OK;}\n'+methods+'\n};\n'+(root/'fixture-suffix.cpp').read_text()
 f=root/f'fixture-{label}.cpp';f.write_text(text)
 subprocess.run(['clang++','-std=c++20','-fsanitize=address,undefined','-g',str(f),'-o',str(root/f'fixture-{label}')],check=True)
 result=subprocess.run([str(root/f'fixture-{label}')]+(['original']if label=='baseline'else[]),text=True,capture_output=True,check=True)
 (root/f'{label}-test.log').write_text(result.stdout+result.stderr); print(result.stdout.strip())
(root/'source.patch').write_text(''.join(difflib.unified_diff(old.read_text().splitlines(True),new.read_text().splitlines(True),fromfile='a/src/d3d11/d3d11_swapchain.cpp',tofile='b/src/d3d11/d3d11_swapchain.cpp')))
(root/'source-binding.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest()for p in [old,new]},indent=2))
