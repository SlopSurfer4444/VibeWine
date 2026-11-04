static void ticks(WineDisplayLink *link, NSUInteger count) { for (NSUInteger i=0; i<count; ++i) [link fire]; }
int main(void) {
    @autoreleasepool {
        queued = [NSMutableArray new];
        WineDisplayLink *link = [[WineDisplayLink alloc] initWithDisplayID:1];
        WineWindow *old = [WineWindow new]; old->dirty = YES;
        [link addWindow:old];
        assert(starts == 1);
        ticks(link, 10000);
        if (pending() != 1 || highWater != 1) { fprintf(stderr, "FAIL stalled consumer: pending=%lu highWater=%lu (expected 1)\n", (unsigned long)pending(), (unsigned long)highWater); return 1; }
        [link removeWindow:old]; [old release];
        assert(windowsReleased == 1); // No queued snapshot retains removed member.
        WineWindow *current = [WineWindow new]; current->dirty = YES;
        [link addWindow:current];
        drainOne();
        assert(current->displays == 1 && pending() == 0); // Consumer samples latest membership.
        puts("PASS stalled consumer: 10000 ticks coalesce; removed window freed; latest window drawn");

        current->dirty = YES;
        current->onDisplay = [^{ ticks(link, 1000); assert(pending() == 0); } copy];
        [link fire]; drainOne();
        assert(current->displays == 2 && pending() == 0);
        [current->onDisplay release]; current->onDisplay = nil;
        current->dirty = YES; [link fire]; drainOne();
        assert(current->displays == 3);
        puts("PASS reentrant ticks do not enqueue while delivery executes; later tick delivers");

        dispatch_group_t group = dispatch_group_create();
        for (int i=0; i<8; i++) dispatch_group_async(group, dispatch_get_global_queue(0,0), ^{ ticks(link, 10000); });
        dispatch_group_wait(group, DISPATCH_TIME_FOREVER);
        assert(pending() == 1 && highWater == 1); drainOne();
        dispatch_release(group);
        puts("PASS eight concurrent producers: 80000 ticks still one delivery");

        int stopped = stops;
        fakeNow += 3; [link fire]; drainOne();
        assert(stops == stopped+1);
        int started = starts; [link start];
        assert(starts == started+1);
        [link fire]; drainOne();
        assert(stops == stopped+1); // Restart resets inactivity time.
        current->dirty = YES; fakeNow += 3; [link fire]; drainOne();
        assert(stops == stopped+1); // Real display resets inactivity time.
        fakeNow += 1; [link fire]; drainOne(); assert(stops == stopped+1);
        puts("PASS idle stop and restart/display reset inactivity time");

        current->dirty = YES; current->throws = YES; [link fire];
        BOOL caught = NO;
        @try { drainOne(); } @catch(NSException *exception) { caught = YES; }
        assert(caught && pending() == 0);
        current->throws = NO; [link fire]; assert(pending() == 1); drainOne();
        puts("PASS exception releases snapshot and reopens delivery gate");

        [link removeWindow:current]; [current release];
        assert(windowsReleased == 2);
        [link fire]; int released = linksReleased; [link release];
        assert(linksReleased == released); // Queued block owns receiver until delivery.
        drainOne(); assert(linksReleased == released+1);
        puts("PASS queued block retains receiver through completion; no window/link lifetime leak");
        [queued release];
    }
    return 0;
}
