int main(int argc,char **argv) { @autoreleasepool {
    assert(argc == 2);
    if(!strcmp(argv[1],"growth")) onlineCount=5;
    else if(!strcmp(argv[1],"fail-first")) { onlineCount=3; failRealloc=1; edidMode=1; }
    else if(!strcmp(argv[1],"fail-second")) { onlineCount=5; failRealloc=2; edidMode=1; }
    else if(!strcmp(argv[1],"edid-malloc-fail")) { onlineCount=5; edidMode=1; edidFail=1; }
    else if(!strcmp(argv[1],"empty-edid")) { onlineCount=5; edidMode=2; }
    else abort();
    NSMutableArray *screens=[NSMutableArray array];
    for(unsigned i=0;i<onlineCount;i++) { FixtureScreen *screen=[FixtureScreen new]; screen->ident=i+1; [screens addObject:screen]; [screen release]; }
    fixtureScreens=screens;
    struct macdrv_monitor *monitors=(void*)0x1234; int count=99;
    int result=macdrv_get_monitors(0,&monitors,&count);
    if(failRealloc) {
        assert(result == -1 && monitors == (void*)0x1234 && count == 99);
        assert(frees == (failRealloc == 1 ? 3 : 5)); // Initialized EDIDs + array only.
    } else {
        assert(!result && count == onlineCount);
        for(int i=0;i<count;i++) {
            if(monitors[i].edid || monitors[i].edid_len) { fprintf(stderr,"FAIL uninitialized EDID at monitor %d\n",i); return 1; }
            assert(monitors[i].id == i+1);
        }
        macdrv_free_monitors(monitors,count); assert(frees == 1);
    }
    printf("PASS %s\n",argv[1]);
} return 0; }
