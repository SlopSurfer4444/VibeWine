  ThreadpoolWork *
  RunThreadpoolWork() {
    cs2lab::CompileTimer timer{.kind="shader", .name=func_name.c_str()};
    auto pool = WMT::MakeAutoreleasePool();
    WMT::Reference<WMT::Error> err;
    WMT::Reference<WMT::DispatchData> lib_data = shader_->find_cached_variant(variant_digest_);

    while (lib_data != nullptr) {
      auto library = device_->GetMTLDevice().newLibrary(lib_data, err);

      if (err || !library) {
        ERR("Failed to create MTLLibrary from cache: ", err.description().getUTF8String());
        lib_data = nullptr;
        break;
      }

      function_ = library.newFunction(func_name.c_str());

      if (function_ == nullptr) {
        ERR("Failed to create MTLFunction from cache: ", func_name);
        lib_data = nullptr;
        break;
      }
      break;
    }

    timer.cache_hit = lib_data != nullptr;
    if (!lib_data) {
      SM50_COMPILED_BITCODE bitcode;
      sm50_bitcode_t compile_result = proc(func_name.c_str(), &sm50_common);

      if (!compile_result)
        return this;

      SM50GetCompiledBitcode(compile_result, &bitcode);
      lib_data = WMT::MakeDispatchData(bitcode.Data, bitcode.Size);
      auto library = device_->GetMTLDevice().newLibrary(lib_data, err);

      if (err) {
        ERR("Failed to create MTLLibrary: ", err.description().getUTF8String());
        shader_->dump();
        SM50DestroyBitcode(compile_result);
        return this;
      }

      shader_->update_cached_variant(variant_digest_, lib_data);

      SM50DestroyBitcode(compile_result);
      function_ = library.newFunction(func_name.c_str());

      if (function_ == nullptr) {
        ERR("Failed to create MTLFunction: ", func_name);
        shader_->dump();
      }
    }

    return this;
  }
