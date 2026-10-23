#include "drawable_phase_diag.h"
int main(void) {
    struct w7pd_sample s=w7pd_begin(); unsigned i;
    if (!s.active) return 1;
    for(i=0;i<5;++i) {s.wall[i]=1000000000 + i*25000000; s.cpu[i]=200+i*2;}
    w7pd_report(&s,"drawable",0x11,0x22,0x33,0x44,0,0,-1);
    s.cpu_valid=false;
    w7pd_report(&s,"drawable",0x11,0x22,0x33,0x44,0,0,-1);
    return 0;
}
