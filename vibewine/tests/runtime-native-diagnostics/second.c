#include <stdatomic.h>
#include <unistd.h>
static unsigned attempts;
static ssize_t count_write(int fd,const void *p,size_t n) {(void)fd;(void)p;attempts++;return n;}
#define W7PD_WRITE count_write
#include "drawable_phase_diag.h"
unsigned second_site(void) {
 unsigned i,j;
 for(i=0;i<300;++i) {
  struct w7pd_sample s=w7pd_begin();
  for(j=0;j<5;++j) {s.wall[j]=1000+j*25000000;s.cpu[j]=100+j;}
  w7pd_report(&s,"other-site",1,2,3,4,0,0,-1);
 }
 return attempts;
}
