; ModuleID = 'default'
source_filename = "airconv_generated.metal"
target datalayout = "e-p:64:64:64-i1:8:8-i8:8:8-i16:16:16-i32:32:32-i64:64:64-f32:32:32-f64:64:64-v16:16:16-v24:32:32-v32:32:32-v48:64:64-v64:64:64-v96:128:128-v128:128:128-v192:256:256-v256:256:256-v512:512:512-v1024:1024:1024-n8:16:32"
target triple = "air64-apple-macosx15.0.0"

%argument_buffer_struct = type { <4 x i32> addrspace(2)* }

define <{ <4 x float> }> @shader_main(<4 x float> %0, %argument_buffer_struct addrspace(2)* %1) local_unnamed_addr {
entry:
  %2 = bitcast <4 x float> %0 to <4 x i32>
  %3 = shufflevector <4 x i32> %2, <4 x i32> poison, <2 x i32> <i32 0, i32 1>
  %4 = bitcast <2 x i32> %3 to <2 x float>
  %5 = fmul <2 x float> %4, <float 2.000000e+00, float 2.000000e+00>
  %6 = fadd <2 x float> %5, <float -1.000000e+00, float -1.000000e+00>
  %7 = getelementptr inbounds %argument_buffer_struct, %argument_buffer_struct addrspace(2)* %1, i64 0, i32 0
  %8 = load <4 x i32> addrspace(2)*, <4 x i32> addrspace(2)* addrspace(2)* %7, align 8
  %9 = getelementptr <4 x i32>, <4 x i32> addrspace(2)* %8, i64 0, i64 1
  %10 = load i32, i32 addrspace(2)* %9, align 4
  %11 = insertelement <2 x i32> <i32 poison, i32 1065353216>, i32 %10, i64 0
  %12 = bitcast <2 x i32> %11 to <2 x float>
  %13 = fmul <2 x float> %6, %12
  %14 = bitcast <2 x float> %13 to <2 x i32>
  %15 = shufflevector <2 x i32> %14, <2 x i32> poison, <4 x i32> <i32 0, i32 1, i32 undef, i32 undef>
  %16 = insertelement <4 x i32> %15, i32 1069547520, i64 2
  %17 = shufflevector <4 x i32> %16, <4 x i32> poison, <3 x i32> <i32 0, i32 1, i32 2>
  %18 = bitcast <3 x i32> %17 to <3 x float>
  %19 = call float @air.dot.v3f32(<3 x float> %18, <3 x float> %18)
  %20 = call float @air.fast_rsqrt.f32(float %19)
  %21 = insertelement <3 x float> undef, float %20, i64 0
  %22 = shufflevector <3 x float> %21, <3 x float> poison, <3 x i32> zeroinitializer
  %23 = fmul <3 x float> %22, %18
  br label %loop_entrance

loop_entrance:                                    ; preds = %end_if, %entry
  %.sroa.54.0 = phi i32 [ 0, %entry ], [ %87, %end_if ]
  %.sroa.64.0 = phi i32 [ 0, %entry ], [ %83, %end_if ]
  %.sroa.82.0 = phi i32 [ 0, %entry ], [ %88, %end_if ]
  %24 = icmp sgt i32 %.sroa.82.0, 95
  br i1 %24, label %end_loop, label %end_if

end_if:                                           ; preds = %loop_entrance
  %25 = bitcast i32 %.sroa.54.0 to float
  %.splatinsert3 = insertelement <3 x i32> poison, i32 %.sroa.54.0, i64 0
  %26 = bitcast <3 x i32> %.splatinsert3 to <3 x float>
  %27 = shufflevector <3 x float> %26, <3 x float> poison, <3 x i32> zeroinitializer
  %28 = fmul <3 x float> %23, %27
  %29 = fadd <3 x float> %28, <float 0.000000e+00, float 0.000000e+00, float 0xC00CCCCCC0000000>
  %30 = bitcast <3 x float> %29 to <3 x i32>
  %31 = extractelement <3 x i32> %30, i64 1
  %32 = bitcast %argument_buffer_struct addrspace(2)* %1 to float addrspace(2)* addrspace(2)*
  %33 = load float addrspace(2)*, float addrspace(2)* addrspace(2)* %32, align 8
  %34 = load float, float addrspace(2)* %33, align 4
  %35 = fmul float %34, 0x3FC99999A0000000
  %36 = call float @air.fast_cos.f32(float %35)
  %37 = call float @air.fast_sin.f32(float %35)
  %38 = insertelement <2 x float> undef, float %36, i64 0
  %39 = shufflevector <2 x float> %38, <2 x float> poison, <2 x i32> zeroinitializer
  %40 = shufflevector <3 x i32> %30, <3 x i32> undef, <2 x i32> <i32 0, i32 2>
  %41 = bitcast <2 x i32> %40 to <2 x float>
  %42 = fmul <2 x float> %39, %41
  %43 = extractelement <2 x float> %42, i64 0
  %44 = insertelement <2 x float> undef, float %37, i64 0
  %45 = shufflevector <2 x float> %44, <2 x float> poison, <2 x i32> zeroinitializer
  %46 = shufflevector <3 x i32> %30, <3 x i32> undef, <2 x i32> <i32 2, i32 0>
  %47 = bitcast <2 x i32> %46 to <2 x float>
  %48 = fmul <2 x float> %45, %47
  %49 = extractelement <2 x float> %48, i64 0
  %50 = fsub float %43, %49
  %51 = extractelement <2 x float> %48, i64 1
  %52 = extractelement <2 x float> %42, i64 1
  %53 = fadd float %51, %52
  %54 = bitcast float %53 to i32
  %55 = insertelement <4 x float> undef, float %50, i64 0
  %56 = bitcast <4 x float> %55 to <4 x i32>
  %57 = insertelement <4 x i32> %56, i32 %31, i64 1
  %58 = insertelement <4 x i32> %57, i32 %54, i64 2
  %59 = shufflevector <4 x i32> %58, <4 x i32> poison, <3 x i32> <i32 0, i32 1, i32 2>
  %60 = bitcast <3 x i32> %59 to <3 x float>
  %61 = call float @air.dot.v3f32(<3 x float> %60, <3 x float> %60)
  %62 = call float @air.fast_sqrt.f32(float %61)
  %63 = fadd float %62, 0xBFECCCCCC0000000
  %64 = fmul float %50, 9.000000e+00
  %65 = fadd float %64, %34
  %66 = call float @air.fast_sin.f32(float %65)
  %67 = bitcast i32 %31 to float
  %68 = fmul float %67, 9.000000e+00
  %69 = call float @air.fast_sin.f32(float %68)
  %70 = fmul float %66, %69
  %71 = fmul float %53, 9.000000e+00
  %72 = call float @air.fast_sin.f32(float %71)
  %73 = fmul float %70, %72
  %74 = fmul float %73, 0x3FAC28F5C0000000
  %75 = fadd float %63, %74
  %76 = call float @air.fast_fabs.f32(float %75)
  %77 = fmul float %76, -3.200000e+01
  %78 = fmul float %77, 0x3FF7154760000000
  %79 = call float @air.fast_exp2.f32(float %78)
  %80 = fmul float %79, 0x3F889374C0000000
  %81 = bitcast i32 %.sroa.64.0 to float
  %82 = fadd float %80, %81
  %83 = bitcast float %82 to i32
  %84 = fmul float %76, 0x3FD99999A0000000
  %85 = call float @air.fast_fmax.f32(float %84, float 0x3F80624DE0000000)
  %86 = fadd float %85, %25
  %87 = bitcast float %86 to i32
  %88 = add i32 %.sroa.82.0, 1
  br label %loop_entrance

end_loop:                                         ; preds = %loop_entrance
  %89 = bitcast i32 %.sroa.64.0 to float
  %90 = fmul float %89, 0x3FE3333340000000
  %91 = fmul float %89, 0x3FEB333340000000
  %92 = insertelement <4 x float> undef, float %90, i64 0
  %93 = insertelement <4 x float> %92, float %91, i64 1
  %94 = bitcast <4 x float> %93 to <4 x i32>
  %95 = insertelement <4 x i32> %94, i32 1065353216, i64 3
  %96 = insertelement <4 x i32> %95, i32 %.sroa.64.0, i64 2
  %97 = bitcast <4 x i32> %96 to <4 x float>
  %98 = insertvalue <{ <4 x float> }> zeroinitializer, <4 x float> %97, 0
  ret <{ <4 x float> }> %98
}

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.dot.v3f32(<3 x float>, <3 x float>) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_rsqrt.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_cos.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_sin.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_sqrt.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare <2 x float> @air.fast_fabs.v2f32(<2 x float>) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_fabs.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_exp2.f32(float) local_unnamed_addr #0

; Function Attrs: mustprogress nofree nosync nounwind readnone willreturn
declare float @air.fast_fmax.f32(float, float) local_unnamed_addr #0

attributes #0 = { mustprogress nofree nosync nounwind readnone willreturn }

!llvm.module.flags = !{!0, !1, !2, !3, !4, !5, !6, !7, !8}
!air.version = !{!9}
!air.language_version = !{!10}
!air.fragment = !{!11}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 0]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 7, !"frame-pointer", i32 2}
!3 = !{i32 7, !"air.max_device_buffers", i32 31}
!4 = !{i32 7, !"air.max_constant_buffers", i32 31}
!5 = !{i32 7, !"air.max_threadgroup_buffers", i32 31}
!6 = !{i32 7, !"air.max_textures", i32 128}
!7 = !{i32 7, !"air.max_read_write_textures", i32 8}
!8 = !{i32 7, !"air.max_samplers", i32 16}
!9 = !{i32 2, i32 7, i32 0}
!10 = !{!"Metal", i32 3, i32 2, i32 0}
!11 = !{<{ <4 x float> }> (<4 x float>, %argument_buffer_struct addrspace(2)*)* @shader_main, !12, !14}
!12 = !{!13}
!13 = !{!"air.render_target", i32 0, i32 0, !"air.arg_type_name", !"float4", !"air.arg_name", !"render_target_0"}
!14 = !{!15, !16}
!15 = !{i32 0, !"air.fragment_input", !"user(reg2_0)", !"air.center", !"air.perspective", !"air.arg_type_name", !"float4", !"air.arg_name", !"reg2_0"}
!16 = !{i32 1, !"air.indirect_buffer", !"air.buffer_size", i32 8, !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.struct_type_info", !17, !"air.arg_type_size", i32 8, !"air.arg_type_align_size", i32 8, !"air.arg_type_name", !"argument_buffer_struct", !"air.arg_name", !"cbuffer_table"}
!17 = !{i32 0, i32 8, i32 0, !"uint4", !"cb0", !"air.indirect_argument", !18}
!18 = !{i32 0, !"air.buffer", !"air.location_index", i32 0, i32 1, !"air.read", !"air.address_space", i32 2, !"air.arg_type_size", i32 16, !"air.arg_type_align_size", i32 16, !"air.arg_type_name", !"uint4", !"air.arg_name", !"cb0"}
