#!/usr/bin/env python3
from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise SystemExit(f"{label}: expected one anchor, found {text.count(old)}")
    return text.replace(old, new, 1)

metal_path = Path("src/gpu/metal_backend.mm")
metal = metal_path.read_text()
metal = replace_once(
    metal,
    '#include "gpu/native_pipeline_cache.hpp"\n',
    '#include "gpu/native_pipeline_cache.hpp"\n#include "digitor/native_node_mask_backend.hpp"\n#include "digitor/native_node_shader_contracts.hpp"\n',
    "Metal includes")
metal = replace_once(
    metal,
    'struct MetalConsumerOwner {\n',
    '''struct MetalMatteOwner {
  id<MTLTexture> texture;
  std::vector<std::shared_ptr<void>> upstream;
  bool tracked{};
  ~MetalMatteOwner() {
    if (texture) --metal_live.textures;
    if (tracked) --metal_live.owners;
  }
};
struct MetalUpstreamBundle { std::vector<std::shared_ptr<void>> values; };
struct MetalConsumerOwner {
''',
    "Metal matte owner")
metal = replace_once(
    metal,
    'class MetalBackend final : public IRenderBackend {',
    'class MetalBackend final : public IRenderBackend, public NativeNodeMaskBackend {',
    "Metal inheritance")

metal_methods = r'''
  struct MetalHslConstants {
    float hue[4], saturation[4], luminance[4];
    float clean_black, clean_white;
    std::uint32_t invert, width, height, padding[3];
  };
  static_assert(sizeof(MetalHslConstants) == 80);
  struct MetalWindowConstants {
    float center_x, center_y, width_f, height_f;
    float rotation, feather, opacity;
    std::uint32_t shape, invert, width, height, padding;
  };
  static_assert(sizeof(MetalWindowConstants) == 48);
  struct MetalSizeConstants { std::uint32_t width, height; };

  id<MTLTexture> make_node_texture(MTLPixelFormat format,
                                   std::uint32_t width,
                                   std::uint32_t height,
                                   LocalCounts& local) noexcept {
    MTLTextureDescriptor* d = [MTLTextureDescriptor
        texture2DDescriptorWithPixelFormat:format width:width height:height
                                  mipmapped:NO];
    d.storageMode = MTLStorageModePrivate;
    d.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
    return make_texture(GpuFailurePoint::OutputResourceCreation,
                        "newTextureWithDescriptor(native node)", d, local);
  }

  DigitorResult dispatch_node_msl(NativeNodeKernel kernel,
                                  std::uint32_t width,
                                  std::uint32_t height,
                                  std::span<id<MTLTexture> const> textures,
                                  const void* constants,
                                  std::size_t constant_bytes) noexcept {
    const auto contract = native_node_pipeline_contract(DIGITOR_RENDERER_METAL, kernel);
    if (!validate_native_node_pipeline_contract(contract) ||
        !constants || constant_bytes != contract.constant_bytes)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    @autoreleasepool {
      @try {
        NSString* source = [[NSString alloc]
            initWithBytes:contract.source.data()
                   length:contract.source.size()
                 encoding:NSUTF8StringEncoding];
        NSString* entry = [[NSString alloc]
            initWithBytes:contract.entry_point.data()
                   length:contract.entry_point.size()
                 encoding:NSUTF8StringEncoding];
        NSError* error = nil;
        id<MTLLibrary> library = [device_ newLibraryWithSource:source options:nil error:&error];
        id<MTLFunction> function = library ? [library newFunctionWithName:entry] : nil;
        id<MTLComputePipelineState> pipeline = function
            ? [device_ newComputePipelineStateWithFunction:function error:&error]
            : nil;
        if (!pipeline) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        LocalCounts local;
        id<MTLCommandQueue> queue = make_queue("newCommandQueue(native node)", local);
        id<MTLCommandBuffer> command = queue
            ? make_command(queue, "commandBuffer(native node)", local) : nil;
        id<MTLComputeCommandEncoder> encoder = command
            ? make_compute_encoder(command, local) : nil;
        MetalEncoderGuard guard(encoder);
        if (!queue || !command || !encoder)
          return abort_encoder(guard, DIGITOR_RESULT_BACKEND_UNAVAILABLE);
        [encoder setComputePipelineState:pipeline];
        for (NSUInteger i = 0; i < textures.size(); ++i)
          [encoder setTexture:textures[i] atIndex:i];
        [encoder setBytes:constants length:constant_bytes atIndex:0];
        [encoder dispatchThreads:MTLSizeMake(width, height, 1)
            threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
        if (!end_encoder(guard, "native node endEncoding") ||
            !commit_and_wait(command))
          return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
        return DIGITOR_RESULT_OK;
      } @catch (...) {
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
    }
  }

public:
  [[nodiscard]] NativeNodeMaskCapabilities
  native_node_mask_capabilities() const noexcept override {
    return {true, true, true, true};
  }

  DigitorResult generate_hsl_matte(
      const GpuSourceResource& source, std::int64_t timestamp,
      const HslQualifierParameters& parameters,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!source.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto prior = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*source.frame));
    if (!prior || !prior->output) return DIGITOR_RESULT_INVALID_ARGUMENT;
    LocalCounts local;
    id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float,
                                                source.width, source.height, local);
    if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
    const auto& values = parameters.values();
    MetalHslConstants c{};
    const auto set_range = [](float (&target)[4], const QualifierRange& range) {
      target[0] = range.low; target[1] = range.high;
      target[2] = range.softness; target[3] = 0.0f;
    };
    set_range(c.hue, values.hue); set_range(c.saturation, values.saturation);
    set_range(c.luminance, values.luminance);
    c.clean_black = values.clean_black; c.clean_white = values.clean_white;
    c.invert = values.invert ? 1u : 0u; c.width = source.width; c.height = source.height;
    const id<MTLTexture> textures[]{prior->output, texture};
    auto status = dispatch_node_msl(NativeNodeKernel::hsl_matte, source.width,
                                    source.height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto owner = std::make_shared<MetalMatteOwner>();
    owner->texture = texture; owner->upstream.push_back(prior); owner->tracked = true;
    ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{800000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_METAL, backend_context_identity(),
        GpuMatteMetadata{source.width, source.height, timestamp, GpuMatteFormat::r32_float},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult generate_power_window_matte(
      std::uint32_t width, std::uint32_t height, std::int64_t timestamp,
      const PowerWindowSettings& settings,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (!width || !height) return DIGITOR_RESULT_INVALID_ARGUMENT;
    LocalCounts local;
    id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float, width, height, local);
    if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
    MetalWindowConstants c{settings.center_x, settings.center_y, settings.width,
      settings.height, settings.rotation, settings.feather, settings.opacity,
      static_cast<std::uint32_t>(settings.shape), settings.invert ? 1u : 0u,
      width, height, 0u};
    const id<MTLTexture> textures[]{texture};
    auto status = dispatch_node_msl(NativeNodeKernel::power_window_matte,
                                    width, height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto owner = std::make_shared<MetalMatteOwner>();
    owner->texture = texture; owner->tracked = true; ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{900000};
    output = std::make_shared<GpuMatteResource>(
        DIGITOR_RENDERER_METAL, backend_context_identity(),
        GpuMatteMetadata{width, height, timestamp, GpuMatteFormat::r32_float},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    return DIGITOR_RESULT_OK;
  }

  DigitorResult multiply_mattes(
      std::span<const GpuMatteResourcePtr> inputs, std::int64_t timestamp,
      GpuMatteResourcePtr& output) noexcept override {
    output.reset();
    if (inputs.empty()) return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (inputs.size() == 1) { output = inputs.front(); return DIGITOR_RESULT_OK; }
    GpuMatteResourcePtr current = inputs.front();
    for (std::size_t i = 1; i < inputs.size(); ++i) {
      const auto& rhs = inputs[i];
      if (!current || !rhs ||
          !current->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
          !rhs->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
          current->metadata().width != rhs->metadata().width ||
          current->metadata().height != rhs->metadata().height)
        return DIGITOR_RESULT_INVALID_ARGUMENT;
      auto left = std::static_pointer_cast<MetalMatteOwner>(current->native_owner());
      auto right = std::static_pointer_cast<MetalMatteOwner>(rhs->native_owner());
      LocalCounts local;
      id<MTLTexture> texture = make_node_texture(MTLPixelFormatR32Float,
          current->metadata().width, current->metadata().height, local);
      if (!texture) return DIGITOR_RESULT_OUT_OF_MEMORY;
      MetalSizeConstants c{current->metadata().width, current->metadata().height};
      const id<MTLTexture> textures[]{left->texture, right->texture, texture};
      auto status = dispatch_node_msl(NativeNodeKernel::matte_multiply,
          c.width, c.height, textures, &c, sizeof(c));
      if (status != DIGITOR_RESULT_OK) return status;
      auto owner = std::make_shared<MetalMatteOwner>();
      owner->texture = texture; owner->upstream = {left, right}; owner->tracked = true;
      ++metal_live.owners; --local.textures;
      static std::atomic_uint64_t ids{1000000};
      current = std::make_shared<GpuMatteResource>(
          DIGITOR_RENDERER_METAL, backend_context_identity(),
          GpuMatteMetadata{c.width, c.height, timestamp, GpuMatteFormat::r32_float},
          ids++, std::static_pointer_cast<void>(owner),
          std::make_shared<std::atomic_bool>(true), backend_context_lifetime());
    }
    output = std::move(current);
    return DIGITOR_RESULT_OK;
  }

  DigitorResult composite_with_matte(
      const GpuSourceResource& original, const GpuSourceResource& processed,
      const GpuMatteResourcePtr& matte, std::int64_t timestamp,
      ProcessedGpuFramePtr& output) noexcept override {
    output.reset();
    if (!original.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        !processed.usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        !matte || !matte->usable_by(DIGITOR_RENDERER_METAL, backend_context_identity()) ||
        original.width != processed.width || original.height != processed.height ||
        original.width != matte->metadata().width || original.height != matte->metadata().height)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto a = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*original.frame));
    auto b = std::static_pointer_cast<MetalPreviewOwner>(native_owner(*processed.frame));
    auto m = std::static_pointer_cast<MetalMatteOwner>(matte->native_owner());
    LocalCounts local;
    id<MTLTexture> result = make_node_texture(MTLPixelFormatRGBA32Float,
                                               original.width, original.height, local);
    if (!result) return DIGITOR_RESULT_OUT_OF_MEMORY;
    MetalSizeConstants c{original.width, original.height};
    const id<MTLTexture> textures[]{a->output, b->output, m->texture, result};
    auto status = dispatch_node_msl(NativeNodeKernel::masked_composite,
                                    c.width, c.height, textures, &c, sizeof(c));
    if (status != DIGITOR_RESULT_OK) return status;
    auto bundle = std::make_shared<MetalUpstreamBundle>();
    bundle->values = {a, b, m};
    auto owner = std::make_shared<MetalPreviewOwner>();
    owner->output = result; owner->preview = nil; owner->queue = nil;
    owner->upstream = bundle; owner->textures = 1; owner->tracked = true;
    ++metal_live.owners; --local.textures;
    static std::atomic_uint64_t ids{1100000};
    output = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_METAL,
        GpuFrameMetadata{original.width, original.height,
                         DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,
                         GpuFrameAlpha::straight, timestamp,
                         original.color_metadata_identity},
        ids++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), true);
    bind_frame_context_lifetime(output);
    return DIGITOR_RESULT_OK;
  }

'''
metal = replace_once(metal, '\npublic:\n  explicit MetalBackend', '\n' + metal_methods + '  explicit MetalBackend', "Metal methods")
metal_path.write_text(metal)

# GLES
gles_path = Path("src/gpu/gles_backend.cpp")
gles = gles_path.read_text()
gles = replace_once(
    gles,
    '#include "gpu/native_pipeline_cache.hpp"\n#include <GLES3/gl3.h>\n',
    '#include "gpu/native_pipeline_cache.hpp"\n#include "digitor/native_node_mask_backend.hpp"\n#include "digitor/native_node_shader_contracts.hpp"\n#include <GLES3/gl31.h>\n',
    "GLES includes")
gles = replace_once(
    gles,
    'struct GlPipelineOwner {',
    '''struct GlMatteOwner { GLuint texture{}; EGLContext context{}; std::vector<std::shared_ptr<void>> upstream;
  GlMatteOwner(){++gl_live.frame_owners;}
  ~GlMatteOwner(){delete_texture(texture,context);--gl_live.frame_owners;}
};
struct GlUpstreamBundle { std::vector<std::shared_ptr<void>> values; };
struct GlPipelineOwner {''',
    "GLES matte owner")
gles = replace_once(gles, 'class GlBackend final : public IRenderBackend {',
                    'class GlBackend final : public IRenderBackend, public NativeNodeMaskBackend {',
                    "GLES inheritance")

gles_methods = r'''
  struct GlHslConstants { float hue[4],saturation[4],luminance[4],clean_black,clean_white; std::uint32_t invert,width,height,padding[3]; };
  struct GlWindowConstants { float center_x,center_y,width_f,height_f,rotation,feather,opacity; std::uint32_t shape,invert,width,height,padding; };
  struct GlSizeConstants { std::uint32_t width,height; };

  std::shared_ptr<GlPipelineOwner> node_program(NativeNodeKernel kernel) noexcept {
    const auto contract = native_node_pipeline_contract(DIGITOR_RENDERER_OPENGL_ES, kernel);
    if (!validate_native_node_pipeline_contract(contract)) return {};
    auto context = eglGetCurrentContext();
    NativePipelineCacheKey key{DIGITOR_RENDERER_OPENGL_ES,
      reinterpret_cast<std::uintptr_t>(context),
      "node-mask-gles:" + std::to_string(static_cast<unsigned>(kernel)), 1,
      GpuPrecisionMode::Float32, DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};
    return std::static_pointer_cast<GlPipelineOwner>(pipeline_cache_.get_or_create(key,[&]()->NativePipelineCache::Object{
      const char* source = contract.source.data(); GLint length = static_cast<GLint>(contract.source.size());
      GLuint shader = glCreateShader(GL_COMPUTE_SHADER); if(!shader) return {};
      glShaderSource(shader,1,&source,&length); glCompileShader(shader); GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
      if(!ok){glDeleteShader(shader);return {};}
      auto owner=std::make_shared<GlPipelineOwner>(); owner->context=context; owner->program=glCreateProgram();
      if(!owner->program){glDeleteShader(shader);return {};}
      ++gl_live.programs; glAttachShader(owner->program,shader); glLinkProgram(owner->program); glDeleteShader(shader);
      glGetProgramiv(owner->program,GL_LINK_STATUS,&ok); if(!ok)return {};
      return std::static_pointer_cast<void>(owner);
    }));
  }

  bool make_node_texture(GLuint& texture, GLenum format,
                         std::uint32_t width, std::uint32_t height) noexcept {
    return make_texture(texture, GpuFailurePoint::OutputResourceCreation,
                        GpuFailurePoint::OutputResourceStorage, format,
                        static_cast<GLsizei>(width), static_cast<GLsizei>(height),
                        "native node texture");
  }

  DigitorResult dispatch_node_compute(NativeNodeKernel kernel,
      std::uint32_t width, std::uint32_t height,
      std::span<const GLuint> textures, const void* constants,
      std::size_t constant_bytes) noexcept {
    const auto contract=native_node_pipeline_contract(DIGITOR_RENDERER_OPENGL_ES,kernel);
    if(!validate_native_node_pipeline_contract(contract)||!constants||constant_bytes!=contract.constant_bytes)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto pipeline=node_program(kernel); if(!pipeline)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    GLuint texture_index=0, ubo=0; glUseProgram(pipeline->program);
    for(std::uint32_t i=0;i<contract.binding_count;++i){const auto& b=contract.bindings[i];
      if(b.kind==NativeNodeBindingKind::constants){glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,constant_bytes,constants,GL_STREAM_DRAW);glBindBufferBase(GL_UNIFORM_BUFFER,b.binding,ubo);continue;}
      if(texture_index>=textures.size()){if(ubo)glDeleteBuffers(1,&ubo);return DIGITOR_RESULT_INVALID_ARGUMENT;}
      GLenum access=b.kind==NativeNodeBindingKind::storage_output?GL_WRITE_ONLY:GL_READ_ONLY;
      GLenum format=b.format=="r32f"?GL_R32F:GL_RGBA32F;
      glBindImageTexture(b.binding,textures[texture_index++],0,GL_FALSE,0,access,format);
    }
    glDispatchCompute((width+7u)/8u,(height+7u)/8u,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);
    glFinish(); if(ubo){glBindBuffer(GL_UNIFORM_BUFFER,0);glDeleteBuffers(1,&ubo);}
    return glGetError()==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

public:
  [[nodiscard]] NativeNodeMaskCapabilities native_node_mask_capabilities() const noexcept override { return {true,true,true,true}; }

  DigitorResult generate_hsl_matte(const GpuSourceResource& source,std::int64_t timestamp,
      const HslQualifierParameters& parameters,GpuMatteResourcePtr& output) noexcept override {
    output.reset(); auto context=eglGetCurrentContext();
    if(!source.usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity()))return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*source.frame)); if(!prior||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner=std::make_shared<GlMatteOwner>(); owner->context=context;
    if(!make_node_texture(owner->texture,GL_R32F,source.width,source.height))return DIGITOR_RESULT_OUT_OF_MEMORY;
    const auto& v=parameters.values(); GlHslConstants c{}; auto set=[](float(&t)[4],const QualifierRange&r){t[0]=r.low;t[1]=r.high;t[2]=r.softness;t[3]=0;};
    set(c.hue,v.hue);set(c.saturation,v.saturation);set(c.luminance,v.luminance);c.clean_black=v.clean_black;c.clean_white=v.clean_white;c.invert=v.invert?1u:0u;c.width=source.width;c.height=source.height;
    const GLuint textures[]{prior->output,owner->texture}; auto status=dispatch_node_compute(NativeNodeKernel::hsl_matte,source.width,source.height,textures,&c,sizeof(c));if(status!=DIGITOR_RESULT_OK)return status;
    owner->upstream.push_back(prior);static std::atomic_uint64_t ids{1200000};output=std::make_shared<GpuMatteResource>(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity(),GpuMatteMetadata{source.width,source.height,timestamp,GpuMatteFormat::r32_float},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),backend_context_lifetime());return DIGITOR_RESULT_OK;
  }
  DigitorResult generate_power_window_matte(std::uint32_t width,std::uint32_t height,std::int64_t timestamp,const PowerWindowSettings& settings,GpuMatteResourcePtr& output) noexcept override {
    output.reset();auto context=eglGetCurrentContext();if(!width||!height||context==EGL_NO_CONTEXT)return DIGITOR_RESULT_INVALID_ARGUMENT;auto owner=std::make_shared<GlMatteOwner>();owner->context=context;if(!make_node_texture(owner->texture,GL_R32F,width,height))return DIGITOR_RESULT_OUT_OF_MEMORY;GlWindowConstants c{settings.center_x,settings.center_y,settings.width,settings.height,settings.rotation,settings.feather,settings.opacity,static_cast<std::uint32_t>(settings.shape),settings.invert?1u:0u,width,height,0u};const GLuint textures[]{owner->texture};auto status=dispatch_node_compute(NativeNodeKernel::power_window_matte,width,height,textures,&c,sizeof(c));if(status!=DIGITOR_RESULT_OK)return status;static std::atomic_uint64_t ids{1300000};output=std::make_shared<GpuMatteResource>(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity(),GpuMatteMetadata{width,height,timestamp,GpuMatteFormat::r32_float},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),backend_context_lifetime());return DIGITOR_RESULT_OK;
  }
  DigitorResult multiply_mattes(std::span<const GpuMatteResourcePtr> inputs,std::int64_t timestamp,GpuMatteResourcePtr& output) noexcept override {
    output.reset();if(inputs.empty())return DIGITOR_RESULT_INVALID_ARGUMENT;if(inputs.size()==1){output=inputs.front();return DIGITOR_RESULT_OK;}GpuMatteResourcePtr current=inputs.front();for(std::size_t i=1;i<inputs.size();++i){auto rhs=inputs[i];if(!current||!rhs||!current->usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity())||!rhs->usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity())||current->metadata().width!=rhs->metadata().width||current->metadata().height!=rhs->metadata().height)return DIGITOR_RESULT_INVALID_ARGUMENT;auto a=std::static_pointer_cast<GlMatteOwner>(current->native_owner());auto b=std::static_pointer_cast<GlMatteOwner>(rhs->native_owner());auto owner=std::make_shared<GlMatteOwner>();owner->context=eglGetCurrentContext();if(!make_node_texture(owner->texture,GL_R32F,current->metadata().width,current->metadata().height))return DIGITOR_RESULT_OUT_OF_MEMORY;GlSizeConstants c{current->metadata().width,current->metadata().height};const GLuint textures[]{a->texture,b->texture,owner->texture};auto status=dispatch_node_compute(NativeNodeKernel::matte_multiply,c.width,c.height,textures,&c,sizeof(c));if(status!=DIGITOR_RESULT_OK)return status;owner->upstream={a,b};static std::atomic_uint64_t ids{1400000};current=std::make_shared<GpuMatteResource>(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity(),GpuMatteMetadata{c.width,c.height,timestamp,GpuMatteFormat::r32_float},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),backend_context_lifetime());}output=std::move(current);return DIGITOR_RESULT_OK;
  }
  DigitorResult composite_with_matte(const GpuSourceResource& original,const GpuSourceResource& processed,const GpuMatteResourcePtr& matte,std::int64_t timestamp,ProcessedGpuFramePtr& output) noexcept override {
    output.reset();auto context=eglGetCurrentContext();if(!original.usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity())||!processed.usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity())||!matte||!matte->usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity())||original.width!=processed.width||original.height!=processed.height||original.width!=matte->metadata().width||original.height!=matte->metadata().height)return DIGITOR_RESULT_INVALID_ARGUMENT;auto a=std::static_pointer_cast<GlPreviewOwner>(native_owner(*original.frame));auto b=std::static_pointer_cast<GlPreviewOwner>(native_owner(*processed.frame));auto m=std::static_pointer_cast<GlMatteOwner>(matte->native_owner());auto owner=std::make_shared<GlPreviewOwner>();owner->context=context;if(!make_node_texture(owner->output,GL_RGBA32F,original.width,original.height)||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"native node composite framebuffer"))return DIGITOR_RESULT_OUT_OF_MEMORY;GlSizeConstants c{original.width,original.height};const GLuint textures[]{a->output,b->output,m->texture,owner->output};auto status=dispatch_node_compute(NativeNodeKernel::masked_composite,c.width,c.height,textures,&c,sizeof(c));if(status!=DIGITOR_RESULT_OK)return status;auto bundle=std::make_shared<GlUpstreamBundle>();bundle->values={a,b,m};owner->upstream=bundle;static std::atomic_uint64_t ids{1500000};output=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{original.width,original.height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,original.color_metadata_identity},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);bind_frame_context_lifetime(output);return DIGITOR_RESULT_OK;
  }

'''
gles = replace_once(gles, '\npublic:\n  GlBackend()', '\n' + gles_methods + '  GlBackend()', "GLES methods")
gles_path.write_text(gles)
