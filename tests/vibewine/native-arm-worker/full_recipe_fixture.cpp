/* SPDX-License-Identifier: MIT
 * Adapted from the owned 2026-10-28 full_recipe_trial.cpp validation source. */
#ifdef NDEBUG
#error "bridge acceptance fixtures require assertions (-UNDEBUG)"
#endif
#define main ordinary_trial_main
#include "bridge_fixture.cpp"
#undef main
#include "airconv_worker_recipe.hpp"
int main(int argc,char** argv){
 assert(argc==2);auto bytes=read(argv[1]);sm50_shader_t shader=nullptr;sm50_error_t error=nullptr;MTL_SHADER_REFLECTION reflection{};
 assert(!WMTWorkerInitialize(bytes.data(),bytes.size(),&shader,&reflection,&error));
 // Owned minimal DXBC RTS0 container: root signature v1.1 with zero parameters/samplers.
 const uint32_t root[]={0x43425844,0,0,0,0,1,68,1,36,0x30535452,24,2,0,24,0,24,0};
 dxmt::worker::OwnedRecipe r;r.function="full_recipe";r.order={1,2,3,4,5,6,7,8,9};
 r.streamOutput.num_output_slots=1;r.streamOutput.strides[0]=16;
 for(unsigned c=0;c<4;++c)r.streamOutputElements.push_back({0,c,0,c});
 r.common.metal_version=SM50_SHADER_METAL_320;r.common.flags=SM50_SHADER_FLAG_DEFUSE_FMA;
 r.pixel.sample_mask=0x55555555;r.pixel.disable_depth_output=true;r.pixel.unorm_output_reg_mask=1;r.pixel.pixel_formats[0]=70;
 r.inputLayout.index_buffer_format=SM50_INDEX_BUFFER_FORMAT_NONE;r.inputLayout.slot_mask=1;
 SM50_IA_INPUT_ELEMENT ia{};ia.reg=0;ia.slot=0;ia.format=31;ia.step_rate=1;r.inputElements.push_back(ia);
 r.passThrough.DataEncoded=0xffffffff;r.passThrough.RasterizationDisabled=true;r.geometry.strip_topology=true;r.tessellator.max_potential_tess_factor=64;
 auto* begin=reinterpret_cast<const unsigned char*>(root);r.rootSignatureBytes.assign(begin,begin+sizeof root);r.rootSignature2Bytes=r.rootSignatureBytes;
 double a,b;auto expected=compile(shader,r.args(),r.function.c_str(),false,a);assert(!expected.empty());auto actual=compile(shader,r.args(),r.function.c_str(),true,b);assert(actual==expected);assert(WMTWorkerTestSuccesses()==1);
 printf("{\"remote_successes\":1,\"all_nine_nodes\":true,\"air_equal\":true,\"bytes\":%zu,\"direct_ms\":%.4f,\"remote_ms\":%.4f}\n",actual.size(),a,b);WMTWorkerDestroy(shader);WMTWorkerShutdown();
}
