// Appended after the actual extracted class/constructor/begin/clear/end.
using namespace dxmt;
static Rc<Texture> texture(WMTPixelFormat format,unsigned samples,unsigned layers=1) {
 return std::make_shared<Texture>(Texture{format,samples,32,24,layers});
}
static void check_clear(ClearRenderTargetContext &clear,ArgumentEncodingContext &ctx,const Rc<Texture> &target) {
 const auto draws=ctx.draws,ends=ctx.ends;
 clear.begin(target,2);
 require(ctx.active,"successful begin must create a pass");
 require(ctx.pso.pso && ctx.pso.pso.value->samples==target->samples,"selected PSO sample count mismatches attachment");
 require(ctx.pso.pso.value->format==target->format,"selected PSO format mismatches attachment");
 require(ctx.pass.default_raster_sample_count==target->samples,"pass sample count changed");
 require((target->format==DepthFormat?ctx.pass.depth.attachment:ctx.pass.colors[0].attachment)==target,"wrong clear attachment");
 const int expected_shader=target->format==DepthFormat?2:target->format==UIntFormat?5:target->format==SIntFormat?4:3;
 require(ctx.pso.pso.value->fragment==expected_shader,"wrong clear shader branch");
 require(ctx.pass.render_target_width==32 && ctx.pass.render_target_height==24 && ctx.pass.render_target_array_length==target->layers,"wrong pass geometry");
 const std::array<float,4> color={.25f,.5f,.75f,1.f};
 clear.clear(3,5,16,12,color);
 require(ctx.draws==draws+1 && ctx.draw.vertex_count==3 && ctx.draw.instance_count==target->layers,"partial clear did not emit expected draw");
 require(ctx.scissor.scissor_rect.x==3 && ctx.scissor.scissor_rect.y==5 && ctx.scissor.scissor_rect.width==16 && ctx.scissor.scissor_rect.height==12,"scissor rectangle changed");
 require(ctx.bytes.length==sizeof(color) && ctx.bytes.index==0 && std::memcmp(ctx.bytes.bytes.ptr,color.data(),sizeof(color))==0,"clear color payload changed");
 clear.end();require(!ctx.active && ctx.ends==ends+1,"end must close successful pass");
 const auto commands=ctx.commands;
 clear.clear(1,1,1,1,color);clear.end();
 require(ctx.commands==commands && ctx.ends==ends+1,"end must reset clearing texture; later clear/end are no-ops");
}
static void failed_clear(ClearRenderTargetContext &clear,ArgumentEncodingContext &ctx,const Rc<Texture> &target) {
 const auto starts=ctx.starts,commands=ctx.commands,ends=ctx.ends;
 clear.begin(target,2);clear.clear(3,5,16,12,{1,0,0,1});clear.end();
 require(!ctx.active && ctx.starts==starts && ctx.commands==commands && ctx.ends==ends,"failed begin must not emit a pass/draw/end");
}
static void matrix() {
 WMT::Device device;InternalCommandLibrary lib;ArgumentEncodingContext ctx;
 ClearRenderTargetContext clear(device,lib,ctx);
 auto single=texture(FloatFormat,1),multi=texture(FloatFormat,4);
 check_clear(clear,ctx,single);const auto single_id=ctx.pso.pso.value->identity;
 check_clear(clear,ctx,multi);require(ctx.pso.pso.value->identity!=single_id,"different counts must not reuse a pipeline");
 check_clear(clear,ctx,single);require(ctx.pso.pso.value->identity==single_id,"same format/count should reuse original pipeline");
 require(device.state->attempts==2,"1x/4x/1x must create exactly two pipelines");
 check_clear(clear,ctx,texture(UIntFormat,4,3));check_clear(clear,ctx,texture(SIntFormat,1));
 require(device.state->attempts==4,"distinct formats must have distinct pipelines");
 WMT::Device reverse;ArgumentEncodingContext other;ClearRenderTargetContext reversed(reverse,lib,other);
 check_clear(reversed,other,multi);check_clear(reversed,other,single);check_clear(reversed,other,multi);
 require(reverse.state->attempts==2,"4x/1x/4x must also create two pipelines");
}
static void retry() {
 WMT::Device device;InternalCommandLibrary lib;ArgumentEncodingContext ctx;
 ClearRenderTargetContext clear(device,lib,ctx);auto target=texture(FloatFormat,4);
 device.state->failures=1;failed_clear(clear,ctx,target);
 require(device.state->attempts==1,"first creation failure not injected");
 check_clear(clear,ctx,target);require(device.state->attempts==2,"failed pipeline must be retried next begin");
 const auto id=ctx.pso.pso.value->identity;check_clear(clear,ctx,target);
 require(device.state->attempts==2 && ctx.pso.pso.value->identity==id,"successful retry must then be cached");
}
static void isolation() {
 WMT::Device device;InternalCommandLibrary lib;ArgumentEncodingContext ctx;
 ClearRenderTargetContext clear(device,lib,ctx);auto target=texture(FloatFormat,1);
 device.state->failures=2;failed_clear(clear,ctx,target);failed_clear(clear,ctx,target);
 require(device.state->attempts==2,"each failed begin must retry without negative caching");
 check_clear(clear,ctx,texture(UIntFormat,4));check_clear(clear,ctx,target);
 require(device.state->attempts==4,"failure must not poison original or distinct key");
}
static void depth_and_rejection() {
 WMT::Device device;InternalCommandLibrary lib;ArgumentEncodingContext ctx;
 ClearRenderTargetContext clear(device,lib,ctx);
 check_clear(clear,ctx,texture(DepthFormat,1));check_clear(clear,ctx,texture(DepthFormat,4));check_clear(clear,ctx,texture(DepthFormat,1));
 require(device.state->attempts==2,"depth clear sample key mismatch");
 failed_clear(clear,ctx,texture(StencilFormat,1));
 require(device.state->attempts==2,"unsupported stencil path should not compile a pipeline");
}
int main(int argc,char **argv) {
 try {
   require(argc==2,"one case required");const std::string name(argv[1]);
   if(name=="matrix") matrix();else if(name=="retry") retry();else if(name=="isolation") isolation();else if(name=="depth") depth_and_rejection();else require(false,"unknown case");
   std::cout<<"PASS "<<name<<"\n";return 0;
 } catch(const std::exception &error) { std::cerr<<"FAIL: "<<error.what()<<"\n";return 2; }
}
