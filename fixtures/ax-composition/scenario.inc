void provider_init(void);
void provider_snapshot(const struct macdrv_ax_request *,struct macdrv_ax_snapshot *);
uint32_t provider_action(const struct macdrv_ax_request *);
unsigned int provider_effects(void);
uint64_t provider_revision(void);
uint64_t bridge_time(void) { return test_ms; }
int bridge_validate(const struct macdrv_ax_request *request)
{
 struct macdrv_ax_request r=*request; r.operation=MACDRV_AX_VALIDATE;
 return !macdrv_ax_poll(&r) && r.result==MACDRV_AX_OK;
}
static void snapshot_read(struct macdrv_ax_request r, BOOL publish)
{
 struct macdrv_ax_snapshot s=response(r);s.count=0;
 provider_snapshot(&r,&s);CHECK(s.result==MACDRV_AX_OK);
 CHECK(provider_revision()==r.revision);
 CHECK(!macdrv_ax_publish(&s));if(publish)drain();
}
int main(void)
{
@autoreleasepool
{
 setenv("WINE_MACDRV_AX_BRIDGE","1",1);provider_init();
 WineWindow *window=[WineWindow new];window->visible=YES;
 ax_register_window(window,(void*)0x1234);
 struct macdrv_ax_request r=poll();snapshot_read(r,YES);
 WineAXElement *element=[ax_windows[0].elements objectAtIndex:0];
 test_ms+=MACDRV_AX_SNAPSHOT_MS;r=poll();CHECK(r.operation==MACDRV_AX_SNAPSHOT);
 snapshot_read(r,NO); /* Real provider binding table now R+1; AppKit still R. */
 CHECK(provider_revision()!=ax_windows[0].revision);
 BOOL accepted=ax_queue_action(element,MACDRV_AX_PRESS,nil);
 if(accepted) {
   struct macdrv_ax_request a=poll();CHECK(a.operation==MACDRV_AX_PRESS);
   CHECK(provider_action(&a)==MACDRV_AX_STALE);CHECK(provider_effects()==0);
   fprintf(stderr,"BUG: native accepted a refresh-race action, actual provider rejected it\n");return 1;
 }
 drain();CHECK(provider_revision()==ax_windows[0].revision);
 CHECK(ax_queue_action(element,MACDRV_AX_PRESS,nil));
 r=poll();CHECK(r.operation==MACDRV_AX_PRESS);
 CHECK(provider_action(&r)==MACDRV_AX_OK);CHECK(provider_effects()==1);
 complete(r,NO);
 /* 100 normal 250-ms refresh epochs: each published tree permits an action,
  * which is dispatched before the next due snapshot. */
 for(unsigned i=0;i<100;i++) {
   test_ms+=MACDRV_AX_SNAPSHOT_MS;
   ax_windows[0].reader_until_ms=test_ms+MACDRV_AX_CACHE_MS;
   r=poll();CHECK(r.operation==MACDRV_AX_SNAPSHOT);snapshot_read(r,YES);
   CHECK(ax_queue_action(element,MACDRV_AX_PRESS,nil));
   r=poll();CHECK(r.operation==MACDRV_AX_PRESS);
   CHECK(provider_action(&r)==MACDRV_AX_OK);complete(r,NO);
 }
 /* Slow provider/main-queue publication must leave an admission interval.
  * Poll immediately after each 300-ms publication before attempting action. */
 for(unsigned i=0;i<10;i++) {
   test_ms+=MACDRV_AX_SNAPSHOT_MS;
   ax_windows[0].reader_until_ms=test_ms+MACDRV_AX_CACHE_MS;
   r=poll();CHECK(r.operation==MACDRV_AX_SNAPSHOT);snapshot_read(r,NO);
   test_ms+=300;drain();
   r=poll();CHECK(r.operation==MACDRV_AX_NONE);
   CHECK(ax_queue_action(element,MACDRV_AX_PRESS,nil));
   r=poll();CHECK(r.operation==MACDRV_AX_PRESS);
   CHECK(provider_action(&r)==MACDRV_AX_OK);complete(r,NO);
 }
 CHECK(provider_effects()==111);
 printf("Composition: %u assertions, 111 actual provider actions/effects, PASS\n",checks);
 ax_unregister_window(window);[window release];
}
return 0;
}
