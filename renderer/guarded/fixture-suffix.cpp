int main(int argc,char**argv){
 bool original=argc>1; int checks=0;
 auto reset=[](){wsi::callback={};wsi::valid=true;wsi::success=true;wsi::calls=0;};
 DXGI_MODE_DESC d; d.Width=1920;d.Height=1080;
 // Exact leave body calls stub WSI synchronously, like SetWindowPos -> WndProc.
 reset(); Test t; HRESULT nested=99; bool caught=false;
 wsi::callback=[&]{nested=t.ResizeTarget(&d);};
 try{assert(t.LeaveFullscreenMode()==S_OK);}catch(const std::runtime_error&){caught=true;}
 if(original){assert(caught);std::cout<<"baseline nonrecursive reacquire reproduced\n";return 0;}
 assert(!caught&&nested==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS&&wsi::calls==1);++checks;
 reset(); assert(t.ResizeTarget(&d)==S_OK);++checks; // guard released after transition
 reset(); nested=99; wsi::callback=[&]{nested=t.ResizeTarget(&d);};
 assert(t.ResizeTarget(&d)==S_OK&&nested==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS&&wsi::calls==1);++checks;
 reset(); {ModeSetGuard::ModeSetInProgress g(t.modeset_guard_); assert(!g); std::thread other([&]{nested=t.ResizeTarget(&d);});other.join();assert(nested==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS&&wsi::calls==0);}++checks;
 reset(); assert(t.ResizeTarget(nullptr)==DXGI_ERROR_INVALID_CALL);assert(t.ResizeTarget(&d)==S_OK);++checks;
 reset();wsi::valid=false;assert(t.ResizeTarget(&d)==DXGI_ERROR_INVALID_CALL);wsi::valid=true;assert(t.ResizeTarget(&d)==S_OK);++checks;
 reset();wsi::success=false;assert(t.LeaveFullscreenMode()==DXGI_ERROR_NOT_CURRENTLY_AVAILABLE);wsi::success=true;assert(t.ResizeTarget(&d)==S_OK);++checks;
 reset();t.fullscreen_desc_.Windowed=false;wsi::callback=[&]{nested=t.ResizeTarget(&d);};assert(t.ResizeTarget(&d)==S_OK&&nested==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS);++checks;
 reset();wsi::callback=[&]{nested=t.LeaveFullscreenMode();};assert(t.ResizeTarget(&d)==S_OK&&nested==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS&&wsi::calls==1);++checks;
 reset();{ModeSetGuard::ModeSetInProgress outer(t.modeset_guard_);{ModeSetGuard::ModeSetInProgress loser(t.modeset_guard_);assert(loser);}assert(t.ResizeTarget(&d)==DXGI_STATUS_MODE_CHANGE_IN_PROGRESS);}assert(t.ResizeTarget(&d)==S_OK);++checks;
 std::cout<<checks<<" exact-method stub checks passed\n";
}
