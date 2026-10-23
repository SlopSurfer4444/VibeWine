#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "visibility_guard.h"
static int checks;
#define CHECK(x) do { assert(x); checks++; } while(0)
static void join(char *out, const char *a, const char *b) { assert(snprintf(out, PATH_MAX, "%s/%s", a, b) < PATH_MAX); }
int main(int argc, char **argv)
{
    char root[PATH_MAX], good[PATH_MAX], other[PATH_MAX], link[PATH_MAX], path[PATH_MAX], renamed[PATH_MAX];
    uint16_t image[512]; size_t n=strlen(W7VIS_DS3_IMAGE); int fd, out; char readback[16]={0};
    assert(argc==2); snprintf(root,sizeof(root),"%s/guards-XXXXXX",argv[1]); assert(mkdtemp(root));
    for(size_t i=0;i<n;i++) image[i]=(unsigned char)W7VIS_DS3_IMAGE[i];
    CHECK(w7vis_exact_ds3_image(image,n*2));
    image[0]='c'; CHECK(w7vis_exact_ds3_image(image,n*2));
    image[n-1]=0; CHECK(!w7vis_exact_ds3_image(image,n*2)); image[n-1]='e';
    CHECK(!w7vis_exact_ds3_image(image,n*2+1)); CHECK(!w7vis_exact_ds3_image(image,n*2-2));
    CHECK(!w7vis_exact_ds3_image(NULL,n*2));
    uint16_t steam[]={'C',':','\\','S','t','e','a','m','.','e','x','e'};
    CHECK(!w7vis_exact_ds3_image(steam,sizeof(steam)));
    image[2]='/'; CHECK(!w7vis_exact_ds3_image(image,n*2)); image[2]='\\';
    image[3]='D'; CHECK(!w7vis_exact_ds3_image(image,n*2));
    join(good,root,"good"); assert(!mkdir(good,0700));
    fd=w7vis_open_private_directory(good); CHECK(fd>=0); CHECK(w7vis_directory_still_named(fd,good));
    CHECK(w7vis_publish(fd,good,"{}",2)==0); CHECK(w7vis_publish(fd,good,"bad",3)<0);
    join(path,good,W7VIS_RESULT_NAME); out=open(path,O_RDONLY); assert(out>=0); CHECK(read(out,readback,16)==2 && !memcmp(readback,"{}",2)); close(out);
    CHECK(!fstatat(fd,W7VIS_RESULT_NAME,&(struct stat){0},AT_SYMLINK_NOFOLLOW));
    close(fd);
    /* A fresh destination is essential: EEXIST must not mask missing size or
     * argument guards. Accept the exact cap and reject cap+1 before creation. */
    join(other,root,"size-boundary"); assert(!mkdir(other,0700));
    fd=w7vis_open_private_directory(other); CHECK(fd>=0);
    char payload[32769]; memset(payload,'x',sizeof(payload));
    CHECK(w7vis_publish(fd,other,payload,sizeof(payload))<0);
    CHECK(w7vis_publish(fd,other,NULL,1)<0);
    CHECK(w7vis_publish(fd,other,payload,0)<0);
    CHECK(fstatat(fd,W7VIS_RESULT_NAME,&(struct stat){0},AT_SYMLINK_NOFOLLOW)<0 && errno==ENOENT);
    CHECK(w7vis_publish(fd,other,payload,32768)==0);
    struct stat result; CHECK(!fstatat(fd,W7VIS_RESULT_NAME,&result,AT_SYMLINK_NOFOLLOW));
    CHECK(result.st_size==32768 && (result.st_mode & 07777)==0600 && result.st_nlink==1);
    close(fd);
    join(other,root,"other"); assert(!mkdir(other,0755)); CHECK(w7vis_open_private_directory(other)<0);
    join(link,root,"final-link"); assert(!symlink(good,link)); CHECK(w7vis_open_private_directory(link)<0);
    join(link,root,"ancestor-link"); assert(!symlink(root,link)); join(path,link,"good"); CHECK(w7vis_open_private_directory(path)<0);
    snprintf(path,sizeof(path),"%s/./good",root); CHECK(w7vis_open_private_directory(path)<0);
    snprintf(path,sizeof(path),"%s/other/../good",root); CHECK(w7vis_open_private_directory(path)<0);
    snprintf(path,sizeof(path),"%s/",good); CHECK(w7vis_open_private_directory(path)<0);
    CHECK(w7vis_open_private_directory("relative")<0);
    assert(!chmod(other,0777)); join(path,other,"nested"); assert(!mkdir(path,0700)); CHECK(w7vis_open_private_directory(path)<0);
    assert(!chmod(other,0700)); fd=w7vis_open_private_directory(path); CHECK(fd>=0);
    join(renamed,root,"moved"); assert(!rename(other,renamed)); CHECK(!w7vis_directory_still_named(fd,path)); CHECK(w7vis_publish(fd,path,"{}",2)<0); close(fd);
    join(other,root,"output-symlink"); assert(!mkdir(other,0700)); join(path,other,W7VIS_RESULT_NAME); assert(!symlink(good,path)); fd=w7vis_open_private_directory(other); CHECK(fd>=0); CHECK(w7vis_publish(fd,other,"{}",2)<0); close(fd);
    join(other,root,"changed-mode"); assert(!mkdir(other,0700)); fd=w7vis_open_private_directory(other); CHECK(fd>=0); assert(!chmod(other,0755)); CHECK(w7vis_publish(fd,other,"{}",2)<0); close(fd);
    printf("PASS %d actual-source guard checks\n",checks); return 0;
}
