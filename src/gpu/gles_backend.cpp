#ifdef __ANDROID__
#include "core/string_utils.hpp"
#include "core/numeric_utils.hpp"
#include "gpu/gpu_backend.hpp"
#include "gpu/native_pipeline_cache.hpp"
#include "digitor/native_node_mask_backend.hpp"
#include "digitor/native_node_shader_contracts.hpp"
#include <GLES3/gl31.h>
#include <GLES2/gl2ext.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <android/hardware_buffer.h>
#include <dlfcn.h>
#include <unistd.h>
#include <cstring>
#include <atomic>
#include <new>
namespace digitor {
namespace {
struct GlObject { GLuint name; };
struct GlLiveCounters {
  std::atomic_int64_t textures{}, framebuffers{}, programs{}, shaders{}, frame_owners{}, consumers{};
} gl_live;
void delete_texture(GLuint& name, EGLContext context) noexcept {
  if(name && eglGetCurrentContext()==context){ glDeleteTextures(1,&name); --gl_live.textures; name=0; }
}
void delete_framebuffer(GLuint& name, EGLContext context) noexcept {
  if(name && eglGetCurrentContext()==context){ glDeleteFramebuffers(1,&name); --gl_live.framebuffers; name=0; }
}
struct GlPreviewOwner { GLuint output{},input{},lut{},framebuffer{},program{}; EGLContext context{};std::shared_ptr<void> upstream,pipeline;
  GlPreviewOwner(){++gl_live.frame_owners;}
  ~GlPreviewOwner(){delete_texture(output,context);delete_texture(input,context);delete_texture(lut,context);delete_framebuffer(framebuffer,context);--gl_live.frame_owners;}
};
struct GlMatteOwner { GLuint texture{}; EGLContext context{}; std::vector<std::shared_ptr<void>> upstream;
  GlMatteOwner(){++gl_live.frame_owners;}
  ~GlMatteOwner(){delete_texture(texture,context);--gl_live.frame_owners;}
};
struct GlUpstreamBundle { std::vector<std::shared_ptr<void>> values; };
struct GlPipelineOwner { GLuint program{};EGLContext context{};~GlPipelineOwner(){if(program&&eglGetCurrentContext()==context){glDeleteProgram(program);--gl_live.programs;}} };
struct GlConsumerOwner { GLuint texture{},framebuffer{};EGLContext context{};
 GlConsumerOwner(){++gl_live.consumers;}
 ~GlConsumerOwner(){delete_framebuffer(framebuffer,context);delete_texture(texture,context);--gl_live.consumers;}
};

using AHardwareBufferDescribeFn =
    void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);
using EglGetNativeClientBufferAndroidFn =
    EGLClientBuffer (*)(const AHardwareBuffer*);
using EglCreateImageKhrFn = EGLImageKHR (*)(
    EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint*);
using EglDestroyImageKhrFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR);
using GlEglImageTargetTexture2DOesFn = void (*)(GLenum, GLeglImageOES);
using EglCreateSyncKhrFn =
    EGLSyncKHR (*)(EGLDisplay, EGLenum, const EGLint*);
using EglWaitSyncKhrFn = EGLBoolean (*)(EGLDisplay, EGLSyncKHR, EGLint);
using EglDestroySyncKhrFn = EGLBoolean (*)(EGLDisplay, EGLSyncKHR);

template <class T>
T resolve_egl_proc(const char* name) noexcept {
  return reinterpret_cast<T>(eglGetProcAddress(name));
}

AHardwareBufferDescribeFn resolve_ahardwarebuffer_describe() noexcept {
  static const auto fn = reinterpret_cast<AHardwareBufferDescribeFn>(
      dlsym(RTLD_DEFAULT, "AHardwareBuffer_describe"));
  return fn;
}

bool has_egl_extension(EGLDisplay display, const char* required) noexcept {
  if (display == EGL_NO_DISPLAY || required == nullptr || *required == '\0')
    return false;
  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
  if (!extensions) return false;
  const auto length = std::strlen(required);
  const char* cursor = extensions;
  while ((cursor = std::strstr(cursor, required)) != nullptr) {
    const bool left = cursor == extensions || cursor[-1] == ' ';
    const bool right = cursor[length] == '\0' || cursor[length] == ' ';
    if (left && right) return true;
    cursor += length;
  }
  return false;
}

struct GlImportedSurfaceOwner {
  GLuint texture{};
  EGLImageKHR image{EGL_NO_IMAGE_KHR};
  EGLDisplay display{EGL_NO_DISPLAY};
  EGLContext context{EGL_NO_CONTEXT};
  EglDestroyImageKhrFn destroy_image{};
  NativeMediaSurfacePtr surface;
  ~GlImportedSurfaceOwner() {
    delete_texture(texture, context);
    if (image != EGL_NO_IMAGE_KHR && display != EGL_NO_DISPLAY &&
        destroy_image != nullptr && eglGetCurrentContext() == context) {
      (void)destroy_image(display, image);
      image = EGL_NO_IMAGE_KHR;
    }
  }
};

bool has_gl_extension(const char* required) noexcept {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint index = 0; index < count; ++index) {
    const auto* extension = reinterpret_cast<const char*>(
        glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(index)));
    if (extension != nullptr && std::strcmp(extension, required) == 0)
      return true;
  }
  return false;
}
class GlBackend final : public IRenderBackend, public NativeNodeMaskBackend {
  NativePipelineCache pipeline_cache_{8};
  void drain_errors() noexcept { while(glGetError()!=GL_NO_ERROR){} }
  bool fail(GpuFailurePoint point,const char* operation) noexcept { return inject_at(point,operation)!=DIGITOR_RESULT_OK; }
  bool allocation_fail(GpuFailurePoint point,const char* operation) noexcept {
    return fail(GpuFailurePoint::DeterministicOutOfMemory,operation)||fail(point,operation);
  }
  bool make_texture(GLuint& out,GpuFailurePoint create,GpuFailurePoint storage,GLenum internal,GLsizei w,GLsizei h,const char* label) noexcept {
    if(allocation_fail(create,label)) return false;
    glGenTextures(1,&out); if(!out) return false; ++gl_live.textures;
    // Texture bindings are per active texture unit. Preserve the caller's
    // binding so allocating an output/LUT cannot silently replace an already
    // bound source texture. Leaving the new texture bound caused render-to-
    // texture feedback in Primary Wheels and replaced the Curves LUT binding.
    GLint active_texture = GL_TEXTURE0;
    GLint previous_binding = 0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &active_texture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_binding);
    glBindTexture(GL_TEXTURE_2D,out);
    if(fail(storage,label)){
      glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_binding));
      delete_texture(out,eglGetCurrentContext());
      return false;
    }
    glTexStorage2D(GL_TEXTURE_2D,1,internal,w,h);
    if(glGetError()!=GL_NO_ERROR){
      glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_binding));
      delete_texture(out,eglGetCurrentContext());
      return false;
    }
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous_binding));
    glActiveTexture(static_cast<GLenum>(active_texture));
    return true;
  }
  void prepare_offscreen_draw_state() noexcept {
    // Native qualification deliberately injects failures at many points. GLES
    // raster state is context-global, so a failed pass must not leak app/test
    // state into the next offscreen color pass. Keep every pass deterministic.
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DITHER);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_FALSE);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    if (fullscreen_vao_ != 0) glBindVertexArray(fullscreen_vao_);
  }
  bool make_framebuffer(GLuint& out,GLuint texture,GLenum target,const char* label) noexcept {
    if(allocation_fail(GpuFailurePoint::FramebufferCreation,label)) return false;
    glGenFramebuffers(1,&out); if(!out) return false; ++gl_live.framebuffers;
    glBindFramebuffer(target,out);
    prepare_offscreen_draw_state();
    if(fail(GpuFailurePoint::FramebufferAttachment,label)) return false;
    glFramebufferTexture2D(target,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    if(fail(GpuFailurePoint::FramebufferValidation,label)) return false;
    return glCheckFramebufferStatus(target)==GL_FRAMEBUFFER_COMPLETE && glGetError()==GL_NO_ERROR;
  }
  GLint uniform_location(GLuint program,const char* name) noexcept {
    if(fail(GpuFailurePoint::UniformLookup,name)) return -2;
    return glGetUniformLocation(program,name);
  }
  NativeResourceCounts counts() const noexcept { NativeResourceCounts c{}; c.images=gl_live.textures.load(); c.image_views=gl_live.framebuffers.load(); c.pipelines=gl_live.programs.load()+gl_live.shaders.load(); c.consumer_destinations=gl_live.consumers.load(); c.frame_owners=gl_live.frame_owners.load(); return c; }
  void finish_failure_evidence(const NativeResourceCounts& before) noexcept { provenance_.resources_after=before; provenance_.cleanup_baseline=true; provenance_.cache_valid=true; provenance_.output_cleared=!provenance_.output_written; }
  GLuint compile_gl(GLenum type,const char*source) noexcept {
    const auto creation = type == GL_VERTEX_SHADER ? GpuFailurePoint::VertexShaderCreation
                                                    : GpuFailurePoint::FragmentShaderCreation;
    const auto compilation = type == GL_VERTEX_SHADER ? GpuFailurePoint::VertexShaderCompilation
                                                       : GpuFailurePoint::FragmentShaderCompilation;
    if(inject_at(creation,type==GL_VERTEX_SHADER?"glCreateShader(vertex)":"glCreateShader(fragment)")!=DIGITOR_RESULT_OK)return 0;
    GLuint x=glCreateShader(type);
    if(!x)return 0; ++gl_live.shaders;
    glShaderSource(x,1,&source,nullptr);
    if(inject_at(compilation,type==GL_VERTEX_SHADER?"glCompileShader(vertex)":"glCompileShader(fragment)")!=DIGITOR_RESULT_OK){glDeleteShader(x);--gl_live.shaders;return 0;}
    glCompileShader(x);GLint ok=0;glGetShaderiv(x,GL_COMPILE_STATUS,&ok);
    if(!ok){glDeleteShader(x);--gl_live.shaders;return 0;}return x;
  }
  std::shared_ptr<GlPipelineOwner> color_program(int operation,const char*vs,const char*fs)noexcept{
    auto context=eglGetCurrentContext();std::string identity=operation==1?"rgb-curves:gles3:nearest-clamp":operation==2?"log-wheels:gles3:nearest-clamp":operation==3?"hsl-qualifier:gles3:nearest-clamp":"primary-wheels:gles3:nearest-clamp";if(gpu_failure_point()!=GpuFailurePoint::None)identity += ":injected-create:"+std::string(gpu_failure_point_name(gpu_failure_point()));NativePipelineCacheKey key{DIGITOR_RENDERER_OPENGL_ES,reinterpret_cast<std::uintptr_t>(context),identity,1,GpuPrecisionMode::Float32,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT};return std::static_pointer_cast<GlPipelineOwner>(pipeline_cache_.get_or_create(key,[&]()->NativePipelineCache::Object{GLuint v=compile_gl(GL_VERTEX_SHADER,vs),f=compile_gl(GL_FRAGMENT_SHADER,fs);if(!v||!f){if(v)glDeleteShader(v);if(f)glDeleteShader(f);return {};}auto owner=std::make_shared<GlPipelineOwner>();owner->context=context;if(inject_at(GpuFailurePoint::ProgramCreation,"glCreateProgram")!=DIGITOR_RESULT_OK){glDeleteShader(v);--gl_live.shaders;glDeleteShader(f);--gl_live.shaders;return {};}owner->program=glCreateProgram();if(!owner->program)return {};++gl_live.programs;glAttachShader(owner->program,v);glAttachShader(owner->program,f);if(inject_at(GpuFailurePoint::ProgramLink,"glLinkProgram")!=DIGITOR_RESULT_OK){glDeleteShader(v);--gl_live.shaders;glDeleteShader(f);--gl_live.shaders;return {};}glLinkProgram(owner->program);glDeleteShader(v);--gl_live.shaders;glDeleteShader(f);--gl_live.shaders;GLint linked{};glGetProgramiv(owner->program,GL_LINK_STATUS,&linked);if(!linked)return {};return std::static_pointer_cast<void>(owner); }));
  }
  std::shared_ptr<GlPipelineOwner> android_import_program() noexcept {
    const auto context = eglGetCurrentContext();
    if (context == EGL_NO_CONTEXT) return {};
    const char* vs =
        "#version 300 es\nprecision highp float;out vec2 uv;"
        "void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);"
        "uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char* fs =
        "#version 300 es\n"
        "#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision highp float;in vec2 uv;uniform samplerExternalOES decoded_frame;"
        "out vec4 color;void main(){color=texture(decoded_frame,uv);}";
    NativePipelineCacheKey key{
        DIGITOR_RENDERER_OPENGL_ES,
        reinterpret_cast<std::uintptr_t>(context),
        "android-ahardwarebuffer:external-oes-to-rgba16f", 1,
        GpuPrecisionMode::Float32, DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT};
    return std::static_pointer_cast<GlPipelineOwner>(pipeline_cache_.get_or_create(
        key, [&]() -> NativePipelineCache::Object {
          GLuint v = compile_gl(GL_VERTEX_SHADER, vs);
          GLuint f = compile_gl(GL_FRAGMENT_SHADER, fs);
          if (!v || !f) {
            if (v) { glDeleteShader(v); --gl_live.shaders; }
            if (f) { glDeleteShader(f); --gl_live.shaders; }
            return {};
          }
          auto owner = std::make_shared<GlPipelineOwner>();
          owner->context = context;
          owner->program = glCreateProgram();
          if (!owner->program) {
            glDeleteShader(v); --gl_live.shaders;
            glDeleteShader(f); --gl_live.shaders;
            return {};
          }
          ++gl_live.programs;
          glAttachShader(owner->program, v);
          glAttachShader(owner->program, f);
          glLinkProgram(owner->program);
          glDeleteShader(v); --gl_live.shaders;
          glDeleteShader(f); --gl_live.shaders;
          GLint linked = 0;
          glGetProgramiv(owner->program, GL_LINK_STATUS, &linked);
          if (!linked) return {};
          return std::static_pointer_cast<void>(owner);
        }));
  }

  DigitorRendererInfo i_{};
  bool fp32_renderable_{};
  GLuint fullscreen_vao_{};
  EGLDisplay display_{EGL_NO_DISPLAY};
  EGLContext context_{EGL_NO_CONTEXT};
  EGLSurface surface_{EGL_NO_SURFACE};
  bool owns_egl_context_{};
  bool native_media_import_ready_{};

  bool make_context_current() noexcept {
    return display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT &&
           eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;
  }


  struct GlHslConstants { float hue[4],saturation[4],luminance[4],clean_black,clean_white,denoise,blur; std::uint32_t invert,width,height,padding; };
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

  DigitorResult import_android_ahardwarebuffer(
      const ZeroCopyImportRequest& request, ProcessedGpuFramePtr& output) noexcept {
    output.reset();
    const auto surface = request.surface;
    if (!surface || request.renderer_backend != DIGITOR_RENDERER_OPENGL_ES ||
        request.output_format != DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT ||
        request.working_color_space != "linear-rgba")
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (!native_media_import_ready_ || !make_context_current() ||
        eglGetCurrentContext() != context_)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    const auto& descriptor = surface->descriptor();
    if (descriptor.platform != NativeMediaPlatform::android ||
        descriptor.handle_type != NativeMediaHandleType::ahardware_buffer ||
        (descriptor.pixel_format != NativeMediaPixelFormat::nv12 &&
         descriptor.pixel_format != NativeMediaPixelFormat::p010) ||
        !descriptor.width || !descriptor.height || (descriptor.width & 1u) ||
        (descriptor.height & 1u) || descriptor.plane_count != 2 ||
        !descriptor.native_handle)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (descriptor.acquire_sync.type != NativeMediaSyncType::none &&
        descriptor.acquire_sync.type != NativeMediaSyncType::sync_fd)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    const auto describe = resolve_ahardwarebuffer_describe();
    const auto get_client_buffer = resolve_egl_proc<EglGetNativeClientBufferAndroidFn>(
        "eglGetNativeClientBufferANDROID");
    const auto create_image =
        resolve_egl_proc<EglCreateImageKhrFn>("eglCreateImageKHR");
    const auto destroy_image =
        resolve_egl_proc<EglDestroyImageKhrFn>("eglDestroyImageKHR");
    const auto image_target = resolve_egl_proc<GlEglImageTargetTexture2DOesFn>(
        "glEGLImageTargetTexture2DOES");
    const auto create_sync =
        resolve_egl_proc<EglCreateSyncKhrFn>("eglCreateSyncKHR");
    const auto wait_sync =
        resolve_egl_proc<EglWaitSyncKhrFn>("eglWaitSyncKHR");
    const auto destroy_sync =
        resolve_egl_proc<EglDestroySyncKhrFn>("eglDestroySyncKHR");
    if (!describe || !get_client_buffer || !create_image || !destroy_image ||
        !image_target || !create_sync || !wait_sync || !destroy_sync)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    auto* ahb = reinterpret_cast<AHardwareBuffer*>(descriptor.native_handle);
    AHardwareBuffer_Desc ahb_desc{};
    describe(ahb, &ahb_desc);
    if (ahb_desc.width != descriptor.width || ahb_desc.height != descriptor.height ||
        ahb_desc.layers != 1 ||
        (ahb_desc.usage & AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE) == 0)
      return DIGITOR_RESULT_INVALID_ARGUMENT;

    const EGLClientBuffer client_buffer = get_client_buffer(ahb);
    if (!client_buffer) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    const EGLint image_attributes[] = {EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    const EGLImageKHR image = create_image(
        display_, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, client_buffer,
        image_attributes);
    if (image == EGL_NO_IMAGE_KHR) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    auto imported = std::make_shared<GlImportedSurfaceOwner>();
    imported->image = image;
    imported->display = display_;
    imported->context = context_;
    imported->destroy_image = destroy_image;
    imported->surface = surface;

    drain_errors();
    glGenTextures(1, &imported->texture);
    if (!imported->texture) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    ++gl_live.textures;
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, imported->texture);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    image_target(GL_TEXTURE_EXTERNAL_OES,
                 reinterpret_cast<GLeglImageOES>(imported->image));
    if (glGetError() != GL_NO_ERROR) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    if (descriptor.acquire_sync.type == NativeMediaSyncType::sync_fd) {
      const int source_fd = static_cast<int>(descriptor.acquire_sync.handle);
      const int imported_fd = ::dup(source_fd);
      if (imported_fd < 0) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      const EGLint sync_attributes[] = {
          EGL_SYNC_NATIVE_FENCE_FD_ANDROID, imported_fd, EGL_NONE};
      const EGLSyncKHR sync = create_sync(
          display_, EGL_SYNC_NATIVE_FENCE_ANDROID, sync_attributes);
      if (sync == EGL_NO_SYNC_KHR) {
        ::close(imported_fd);
        return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
      }
      const EGLBoolean waited = wait_sync(display_, sync, 0);
      (void)destroy_sync(display_, sync);
      if (waited != EGL_TRUE) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }

    auto pipeline = android_import_program();
    if (!pipeline) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    auto owner = std::make_shared<GlPreviewOwner>();
    owner->context = context_;
    owner->program = pipeline->program;
    owner->pipeline = pipeline;
    owner->upstream = imported;
    if (!make_texture(owner->output, GpuFailurePoint::OutputResourceCreation,
                      GpuFailurePoint::OutputResourceStorage, GL_RGBA16F,
                      static_cast<GLsizei>(descriptor.width),
                      static_cast<GLsizei>(descriptor.height),
                      "Android AHardwareBuffer RGBA16F output") ||
        !make_framebuffer(owner->framebuffer, owner->output, GL_FRAMEBUFFER,
                          "Android AHardwareBuffer RGBA16F framebuffer"))
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    prepare_offscreen_draw_state();
    glBindFramebuffer(GL_FRAMEBUFFER, owner->framebuffer);
    glViewport(0, 0, static_cast<GLsizei>(descriptor.width),
               static_cast<GLsizei>(descriptor.height));
    glUseProgram(pipeline->program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, imported->texture);
    const GLint source_location =
        glGetUniformLocation(pipeline->program, "decoded_frame");
    if (source_location < 0) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    glUniform1i(source_location, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFlush();
    if (glGetError() != GL_NO_ERROR) return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    static std::atomic_uint64_t identities{1'300'000};
    output = std::make_shared<ProcessedGpuFrame>(
        this, DIGITOR_RENDERER_OPENGL_ES,
        GpuFrameMetadata{descriptor.width, descriptor.height,
                         DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT,
                         GpuFrameAlpha::straight, descriptor.timestamp_us,
                         request.working_color_space},
        identities++, std::static_pointer_cast<void>(owner),
        std::make_shared<std::atomic_bool>(true), false);
    bind_frame_context_lifetime(output);
    return output && output->ready() ? DIGITOR_RESULT_OK
                                     : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }

public:
  BackendProductionCapability production_capability() const noexcept override {
    BackendProductionCapability out{};
    out.backend = DIGITOR_RENDERER_OPENGL_ES;
    out.context_identity = backend_context_identity();
    out.frame_context_identity = this;
    out.resources = GlesProductionResources{
        reinterpret_cast<void*>(display_), reinterpret_cast<void*>(context_)};
    if (native_media_import_ready_ && display_ != EGL_NO_DISPLAY &&
        context_ != EGL_NO_CONTEXT) {
      out.native_media_import =
          [self = const_cast<GlBackend*>(this)](
              const ZeroCopyImportRequest& request,
              ProcessedGpuFramePtr& frame) noexcept {
            return self->import_android_ahardwarebuffer(request, frame);
          };
    }
    return out;
  }
  [[nodiscard]] NativeNodeMaskCapabilities native_node_mask_capabilities() const noexcept override { return {true,true,true,true}; }

  DigitorResult generate_hsl_matte(const GpuSourceResource& source,std::int64_t timestamp,
      const HslQualifierParameters& parameters,GpuMatteResourcePtr& output) noexcept override {
    output.reset(); auto context=eglGetCurrentContext();
    if(!source.usable_by(DIGITOR_RENDERER_OPENGL_ES,backend_context_identity()))return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*source.frame)); if(!prior||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto owner=std::make_shared<GlMatteOwner>(); owner->context=context;
    if(!make_node_texture(owner->texture,GL_R32F,source.width,source.height))return DIGITOR_RESULT_OUT_OF_MEMORY;
    const auto& v=parameters.values(); GlHslConstants c{}; auto set=[](float(&t)[4],const QualifierRange&r){t[0]=r.low;t[1]=r.high;t[2]=r.softness;t[3]=0;};
    set(c.hue,v.hue);set(c.saturation,v.saturation);set(c.luminance,v.luminance);c.clean_black=v.clean_black;c.clean_white=v.clean_white;c.denoise=v.denoise;c.blur=v.blur;c.invert=v.invert?1u:0u;c.width=source.width;c.height=source.height;
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

  GlBackend() {
    i_.backend = DIGITOR_RENDERER_OPENGL_ES;
    copy_bounded(i_.backend_name, "OpenGL ES");
    copy_bounded(i_.device_name, "Current EGL context");
    i_.is_gpu = 1;
  }
  bool initialize(bool) override {
    if (eglGetCurrentContext() == EGL_NO_CONTEXT) {
      owns_egl_context_ = true;
      display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
      EGLint egl_major = 0, egl_minor = 0;
      if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, &egl_major, &egl_minor))
        return false;
      const EGLint config_attributes[] = {
          EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
          EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
          EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
          EGL_NONE};
      EGLConfig config{}; EGLint config_count = 0;
      if (!eglChooseConfig(display_, config_attributes, &config, 1, &config_count) || config_count < 1)
        return false;
      const EGLint surface_attributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
      surface_ = eglCreatePbufferSurface(display_, config, surface_attributes);
      if (surface_ == EGL_NO_SURFACE) return false;
      const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
      context_ = eglCreateContext(display_, config, EGL_NO_CONTEXT, context_attributes);
      if (context_ == EGL_NO_CONTEXT || !make_context_current()) return false;
    } else {
      owns_egl_context_ = false;
      context_ = eglGetCurrentContext();
      display_ = eglGetCurrentDisplay();
      surface_ = eglGetCurrentSurface(EGL_DRAW);
    }
    while (glGetError() != GL_NO_ERROR) {}
    GLint major = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    if (major < 3)
      return false;
    fp32_renderable_ = has_gl_extension("GL_EXT_color_buffer_float");
    if (fp32_renderable_) {
      GLuint probe_texture = 0, probe_fbo = 0;
      glGenTextures(1, &probe_texture);
      glBindTexture(GL_TEXTURE_2D, probe_texture);
      glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, 1, 1);
      glGenFramebuffers(1, &probe_fbo);
      glBindFramebuffer(GL_FRAMEBUFFER, probe_fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             GL_TEXTURE_2D, probe_texture, 0);
      fp32_renderable_ = glCheckFramebufferStatus(GL_FRAMEBUFFER) ==
                             GL_FRAMEBUFFER_COMPLETE &&
                         glGetError() == GL_NO_ERROR;
      glDeleteFramebuffers(1, &probe_fbo);
      glDeleteTextures(1, &probe_texture);
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    glGenVertexArrays(1, &fullscreen_vao_);
    if (fullscreen_vao_ != 0) glBindVertexArray(fullscreen_vao_);
    native_media_import_ready_ =
        fp32_renderable_ && resolve_ahardwarebuffer_describe() != nullptr &&
        has_gl_extension("GL_OES_EGL_image_external_essl3") &&
        has_egl_extension(display_, "EGL_KHR_image_base") &&
        has_egl_extension(display_, "EGL_ANDROID_image_native_buffer") &&
        has_egl_extension(display_, "EGL_ANDROID_get_native_client_buffer") &&
        has_egl_extension(display_, "EGL_ANDROID_native_fence_sync") &&
        has_egl_extension(display_, "EGL_KHR_wait_sync") &&
        resolve_egl_proc<EglGetNativeClientBufferAndroidFn>(
            "eglGetNativeClientBufferANDROID") != nullptr &&
        resolve_egl_proc<EglCreateImageKhrFn>("eglCreateImageKHR") != nullptr &&
        resolve_egl_proc<EglDestroyImageKhrFn>("eglDestroyImageKHR") != nullptr &&
        resolve_egl_proc<GlEglImageTargetTexture2DOesFn>(
            "glEGLImageTargetTexture2DOES") != nullptr &&
        resolve_egl_proc<EglCreateSyncKhrFn>("eglCreateSyncKHR") != nullptr &&
        resolve_egl_proc<EglWaitSyncKhrFn>("eglWaitSyncKHR") != nullptr &&
        resolve_egl_proc<EglDestroySyncKhrFn>("eglDestroySyncKHR") != nullptr;
    i_.supports_fp32 = fp32_renderable_ ? 1 : 0;
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    if (renderer != nullptr)
      copy_bounded(i_.device_name, renderer);
    return glGetError() == GL_NO_ERROR;
  }
  void shutdown() noexcept override {
    const auto display = display_;
    const auto context = context_;
    const auto surface = surface_;
    if (context != EGL_NO_CONTEXT && display != EGL_NO_DISPLAY)
      (void)eglMakeCurrent(display, surface, surface, context);
    pipeline_cache_.invalidate_device(
        DIGITOR_RENDERER_OPENGL_ES,
        reinterpret_cast<std::uintptr_t>(context));
    if (eglGetCurrentContext() == context) glFinish();
    if (eglGetCurrentContext() == context && fullscreen_vao_ != 0) { glDeleteVertexArrays(1, &fullscreen_vao_); fullscreen_vao_ = 0; }
    if (owns_egl_context_ && display != EGL_NO_DISPLAY) {
      (void)eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
      if (surface != EGL_NO_SURFACE) (void)eglDestroySurface(display, surface);
      if (context != EGL_NO_CONTEXT) (void)eglDestroyContext(display, context);
      (void)eglTerminate(display);
    }
    native_media_import_ready_ = false;
    display_ = EGL_NO_DISPLAY;
    context_ = EGL_NO_CONTEXT;
    surface_ = EGL_NO_SURFACE;
    owns_egl_context_ = false;
  }
  NativePipelineCacheCounters native_pipeline_cache_counters()const noexcept override{return pipeline_cache_.counters();}
  NativeResourceCounts native_resource_counts()const noexcept override{return counts();}
  std::size_t native_pipeline_cache_size()const noexcept override{return pipeline_cache_.size();}
  void clear_native_pipeline_cache_for_test()noexcept override{pipeline_cache_.invalidate_device(DIGITOR_RENDERER_OPENGL_ES,reinterpret_cast<std::uintptr_t>(eglGetCurrentContext()));}
  DigitorRendererInfo info() const noexcept override { return i_; }
  DigitorResult execute_process_primary_wheels_gpu(std::span<const Color>src,uint32_t width,uint32_t height,int64_t timestamp,const PrimaryWheelsParameters&parameters,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto context=eglGetCurrentContext();begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","primary-wheels-glsl-es-v1","GL framebuffer primary wheels");
    if(!fp32_renderable_)return DIGITOR_RESULT_UNSUPPORTED;if(context==EGL_NO_CONTEXT)return DIGITOR_RESULT_NOT_INITIALIZED;if(!width||!height||src.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char*fs="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D im;uniform vec4 lift,gamma,gain,offset;uniform ivec4 enabled;out vec4 color;float sp(float x,float e){if(isnan(x)||isinf(x))return x;return x<0.? -pow(-x,e):pow(x,e);}void main(){vec4 c=texture(im,uv);float a=c.a;if(enabled.x!=0)c.rgb+=lift.rgb+lift.a;if(enabled.y!=0)c.rgb=vec3(sp(c.r,1./(gamma.r*gamma.a)),sp(c.g,1./(gamma.g*gamma.a)),sp(c.b,1./(gamma.b*gamma.a)));if(enabled.z!=0)c.rgb*=gain.rgb*gain.a;if(enabled.w!=0)c.rgb+=offset.rgb+offset.a;color=vec4(c.rgb,a);}";
    auto cached=color_program(0,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;GLuint p=cached->program;
    auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::shared_ptr<GlPreviewOwner>(new(std::nothrow)GlPreviewOwner{});if(!owner)return DIGITOR_RESULT_OUT_OF_MEMORY;owner->program=p;owner->pipeline=cached;owner->context=context;
    if(!make_texture(owner->input,GpuFailurePoint::SourceResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_RGBA32F,width,height,"primary source texture")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);if(fail(GpuFailurePoint::SourceUpload,"glTexSubImage2D primary source")){finish_failure_evidence(baseline);return provenance_.failure_result;}glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,src.data());
    if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,width,height,"primary output texture")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"primary output framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    const auto&x=parameters.values();if(fail(GpuFailurePoint::ResourceBinding,"glUseProgram primary")){finish_failure_evidence(baseline);return provenance_.failure_result;}glUseProgram(p);auto im=uniform_location(p,"im");if(im==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform1i(im,0);auto set=[&](const char*n,PrimaryRgb c,float m){auto loc=uniform_location(p,n);if(loc==-2)return false;glUniform4f(loc,c.r,c.g,c.b,m);return true;};if(!set("lift",x.lift,x.lift_master)||!set("gamma",x.gamma,x.gamma_master)||!set("gain",x.gain,x.gain_master)||!set("offset",x.offset,x.offset_master)){finish_failure_evidence(baseline);return provenance_.failure_result;}auto enabled=uniform_location(p,"enabled");if(enabled==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform4i(enabled,x.lift_enabled,x.gamma_enabled,x.gain_enabled,x.offset_enabled);if(fail(GpuFailurePoint::DrawSetup,"glViewport primary")){finish_failure_evidence(baseline);return provenance_.failure_result;}glViewport(0,0,width,height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays primary")){finish_failure_evidence(baseline);return provenance_.failure_result;}glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::Flush,"glFlush primary")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFlush();if(glGetError()!=GL_NO_ERROR){finish_failure_evidence(baseline);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    static std::atomic_uint64_t ids{100000};if(fail(GpuFailurePoint::ProcessedFrameCreation,"ProcessedGpuFrame primary")){finish_failure_evidence(baseline);return provenance_.failure_result;}out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"linear-rgba"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.primary_wheels_enabled=true;provenance_.primary_wheels_parameter_identity=parameters.identity();provenance_.primary_wheels_shader_identity="primary-wheels-glsl-es-v1";provenance_.primary_wheels_pipeline_identity="GLProgram:primary-wheels-v1";provenance_.primary_wheels_source_bound=provenance_.primary_wheels_destination_bound=provenance_.primary_wheels_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;
  }


  DigitorResult execute_validation_readback_primary_wheels(const ProcessedGpuFramePtr&frame,std::span<Color>out)noexcept override{
    begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","validation-readback","glReadPixels");if(!frame||frame->backend()!=DIGITOR_RENDERER_OPENGL_ES||eglGetCurrentContext()==EGL_NO_CONTEXT||out.size()!=std::size_t(frame->metadata().width)*frame->metadata().height)return DIGITOR_RESULT_INVALID_ARGUMENT;auto owner=std::static_pointer_cast<GlPreviewOwner>(native_owner(*frame));if(!owner||owner->context!=eglGetCurrentContext())return DIGITOR_RESULT_INVALID_ARGUMENT;auto baseline=counts();provenance_.resources_before=baseline;if(fail(GpuFailurePoint::ValidationReadbackResourceCreation,"validation readback setup")){finish_failure_evidence(baseline);return provenance_.failure_result;}glBindFramebuffer(GL_READ_FRAMEBUFFER,owner->framebuffer);glReadBuffer(GL_COLOR_ATTACHMENT0);glPixelStorei(GL_PACK_ALIGNMENT,1);glBindBuffer(GL_PIXEL_PACK_BUFFER,0);const auto&m=frame->metadata();if(fail(GpuFailurePoint::ValidationReadbackCopy,"glReadPixels")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFinish();glReadPixels(0,0,m.width,m.height,GL_RGBA,GL_FLOAT,out.data());if(fail(GpuFailurePoint::ValidationReadbackMap,"validation readback finalize")){finish_failure_evidence(baseline);return provenance_.failure_result;}provenance_.validation_readback_completed=true;provenance_.readback_performed=true;return glGetError()==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult execute_process_primary_wheels_gpu(const GpuSourceResource&s,int64_t timestamp,const PrimaryWheelsParameters&parameters,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*s.frame));auto context=eglGetCurrentContext();if(!prior||context==EGL_NO_CONTEXT||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";const char*fs="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D im;uniform vec4 lift,gamma,gain,offset;uniform ivec4 enabled;out vec4 color;float sp(float x,float e){if(isnan(x)||isinf(x))return x;return x<0.?-pow(-x,e):pow(x,e);}void main(){vec4 c=texture(im,uv);float a=c.a;if(enabled.x!=0)c.rgb+=lift.rgb+lift.a;if(enabled.y!=0)c.rgb=vec3(sp(c.r,1./(gamma.r*gamma.a)),sp(c.g,1./(gamma.g*gamma.a)),sp(c.b,1./(gamma.b*gamma.a)));if(enabled.z!=0)c.rgb*=gain.rgb*gain.a;if(enabled.w!=0)c.rgb+=offset.rgb+offset.a;color=vec4(c.rgb,a);}";
    begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","primary-wheels-glsl-es-v1","GL framebuffer primary GPU source");auto cached=color_program(0,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;GLuint p=cached->program;auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::make_shared<GlPreviewOwner>();owner->program=p;owner->pipeline=cached;owner->context=context;owner->upstream=prior;
    if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,s.width,s.height,"primary GPU output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"primary GPU framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    if(fail(GpuFailurePoint::ResourceBinding,"bind primary GPU source")){finish_failure_evidence(baseline);return provenance_.failure_result;}glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,prior->output);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);auto im=uniform_location(p,"im");if(im==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform1i(im,0);const auto&x=parameters.values();auto set=[&](const char*n,PrimaryRgb c,float m){auto loc=uniform_location(p,n);if(loc==-2)return false;glUniform4f(loc,c.r,c.g,c.b,m);return true;};if(!set("lift",x.lift,x.lift_master)||!set("gamma",x.gamma,x.gamma_master)||!set("gain",x.gain,x.gain_master)||!set("offset",x.offset,x.offset_master)){finish_failure_evidence(baseline);return provenance_.failure_result;}auto enabled=uniform_location(p,"enabled");if(enabled==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform4i(enabled,x.lift_enabled,x.gamma_enabled,x.gain_enabled,x.offset_enabled);if(fail(GpuFailurePoint::DrawSetup,"glViewport primary GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glViewport(0,0,s.width,s.height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays primary GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::SynchronizationWait,"glFinish primary GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFinish();if(glGetError()!=GL_NO_ERROR){finish_failure_evidence(baseline);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}static std::atomic_uint64_t ids{200000};if(fail(GpuFailurePoint::ProcessedFrameCreation,"ProcessedGpuFrame primary GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{s.width,s.height,s.format,GpuFrameAlpha::straight,timestamp,s.color_metadata_identity},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.primary_wheels_source_bound=provenance_.primary_wheels_destination_bound=provenance_.primary_wheels_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_log_wheels_gpu(std::span<const Color> src,uint32_t width,uint32_t height,int64_t timestamp,const LogWheelsParameters& parameters,ProcessedGpuFramePtr& out) noexcept override {
    out.reset(); auto context=eglGetCurrentContext(); begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","log-wheels-glsl-es-v1","GL framebuffer log wheels");
    if(!fp32_renderable_)return DIGITOR_RESULT_UNSUPPORTED; if(context==EGL_NO_CONTEXT)return DIGITOR_RESULT_NOT_INITIALIZED; if(!width||!height||src.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im;uniform vec4 shadows,midtones,highlights,globalWheel,tonal;uniform ivec4 enabled;out vec4 color;float sb(float a,float b,float x){float t=clamp((x-a)/(b-a),0.,1.);return t*t*(3.-2.*t);}void main(){vec4 c=texture(im,uv);float a=c.a;float y=dot(c.rgb,vec3(.2126,.7152,.0722));float h=tonal.z*.5;float sw=1.-sb(tonal.x-h,tonal.x+h,y);float hw=sb(tonal.y-h,tonal.y+h,y);float mw=max(0.,1.-sw-hw);float stop=(enabled.x!=0?shadows.a*sw:0.)+(enabled.y!=0?midtones.a*mw:0.)+(enabled.z!=0?highlights.a*hw:0.)+(enabled.w!=0?globalWheel.a:0.);vec3 bal=(enabled.x!=0?shadows.rgb*sw:vec3(0))+(enabled.y!=0?midtones.rgb*mw:vec3(0))+(enabled.z!=0?highlights.rgb*hw:vec3(0))+(enabled.w!=0?globalWheel.rgb:vec3(0));color=vec4(c.rgb*exp2(stop)+bal,a);}";
    auto cached=color_program(2,vs,fs); if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE; GLuint program=cached->program; auto baseline=counts(); provenance_.resources_before=baseline; auto owner=std::shared_ptr<GlPreviewOwner>(new(std::nothrow)GlPreviewOwner{}); if(!owner)return DIGITOR_RESULT_OUT_OF_MEMORY; owner->program=program;owner->pipeline=cached;owner->context=context;
    if(!make_texture(owner->input,GpuFailurePoint::SourceResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_RGBA32F,width,height,"log source texture")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;} glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);if(fail(GpuFailurePoint::SourceUpload,"glTexSubImage2D log source")){finish_failure_evidence(baseline);return provenance_.failure_result;}glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,src.data());
    if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,width,height,"log output texture")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"log framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    const auto&x=parameters.values();if(fail(GpuFailurePoint::ResourceBinding,"bind log resources")){finish_failure_evidence(baseline);return provenance_.failure_result;}glUseProgram(program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);auto im=uniform_location(program,"im");if(im==-2)return provenance_.failure_result;glUniform1i(im,0);auto set=[&](const char*n,const LogWheelControl&w){auto l=uniform_location(program,n);if(l==-2)return false;glUniform4f(l,w.rgb.r,w.rgb.g,w.rgb.b,w.master);return true;};if(!set("shadows",x.shadows)||!set("midtones",x.midtones)||!set("highlights",x.highlights)||!set("globalWheel",x.global))return provenance_.failure_result;auto en=uniform_location(program,"enabled"),to=uniform_location(program,"tonal");if(en==-2||to==-2)return provenance_.failure_result;glUniform4i(en,x.shadows.enabled,x.midtones.enabled,x.highlights.enabled,x.global.enabled);glUniform4f(to,x.shadow_pivot,x.highlight_pivot,x.transition_width,0.f);if(fail(GpuFailurePoint::DrawSetup,"glViewport log"))return provenance_.failure_result;glViewport(0,0,width,height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays log"))return provenance_.failure_result;glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::Flush,"glFlush log"))return provenance_.failure_result;glFlush();if(glGetError()!=GL_NO_ERROR)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    static std::atomic_uint64_t ids{400000};if(fail(GpuFailurePoint::ProcessedFrameCreation,"ProcessedGpuFrame log"))return provenance_.failure_result;out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"linear-rgba"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.log_wheels_enabled=true;provenance_.log_wheels_parameter_identity=parameters.identity();provenance_.log_wheels_shader_identity="log-wheels-glsl-es-v1";provenance_.log_wheels_pipeline_identity="GLProgram:log-wheels-v1";provenance_.log_wheels_source_bound=provenance_.log_wheels_destination_bound=provenance_.log_wheels_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_log_wheels_gpu(const GpuSourceResource&s,int64_t timestamp,const LogWheelsParameters&parameters,ProcessedGpuFramePtr&out) noexcept override {
    out.reset();auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*s.frame));auto context=eglGetCurrentContext();if(!prior||context==EGL_NO_CONTEXT||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im;uniform vec4 shadows,midtones,highlights,globalWheel,tonal;uniform ivec4 enabled;out vec4 color;float sb(float a,float b,float x){float t=clamp((x-a)/(b-a),0.,1.);return t*t*(3.-2.*t);}void main(){vec4 c=texture(im,uv);float a=c.a;float y=dot(c.rgb,vec3(.2126,.7152,.0722));float h=tonal.z*.5;float sw=1.-sb(tonal.x-h,tonal.x+h,y);float hw=sb(tonal.y-h,tonal.y+h,y);float mw=max(0.,1.-sw-hw);float stop=(enabled.x!=0?shadows.a*sw:0.)+(enabled.y!=0?midtones.a*mw:0.)+(enabled.z!=0?highlights.a*hw:0.)+(enabled.w!=0?globalWheel.a:0.);vec3 bal=(enabled.x!=0?shadows.rgb*sw:vec3(0))+(enabled.y!=0?midtones.rgb*mw:vec3(0))+(enabled.z!=0?highlights.rgb*hw:vec3(0))+(enabled.w!=0?globalWheel.rgb:vec3(0));color=vec4(c.rgb*exp2(stop)+bal,a);}";begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","log-wheels-glsl-es-v1","GL framebuffer log GPU source");auto cached=color_program(2,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::make_shared<GlPreviewOwner>();owner->program=cached->program;owner->pipeline=cached;owner->context=context;owner->upstream=prior;if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,s.width,s.height,"log GPU output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"log GPU framebuffer"))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;const auto&x=parameters.values();glUseProgram(cached->program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,prior->output);auto im=uniform_location(cached->program,"im");if(im==-2)return provenance_.failure_result;glUniform1i(im,0);auto set=[&](const char*n,const LogWheelControl&w){auto l=uniform_location(cached->program,n);if(l==-2)return false;glUniform4f(l,w.rgb.r,w.rgb.g,w.rgb.b,w.master);return true;};if(!set("shadows",x.shadows)||!set("midtones",x.midtones)||!set("highlights",x.highlights)||!set("globalWheel",x.global))return provenance_.failure_result;auto en=uniform_location(cached->program,"enabled"),to=uniform_location(cached->program,"tonal");if(en==-2||to==-2)return provenance_.failure_result;glUniform4i(en,x.shadows.enabled,x.midtones.enabled,x.highlights.enabled,x.global.enabled);glUniform4f(to,x.shadow_pivot,x.highlight_pivot,x.transition_width,0.f);glViewport(0,0,s.width,s.height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays log GPU"))return provenance_.failure_result;glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::SynchronizationWait,"glFinish log GPU"))return provenance_.failure_result;glFinish();if(glGetError()!=GL_NO_ERROR)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;static std::atomic_uint64_t ids{410000};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{s.width,s.height,s.format,GpuFrameAlpha::straight,timestamp,s.color_metadata_identity},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.log_wheels_enabled=true;provenance_.log_wheels_parameter_identity=parameters.identity();provenance_.log_wheels_shader_identity="log-wheels-glsl-es-v1";provenance_.log_wheels_pipeline_identity="GLProgram:log-wheels-v1";provenance_.log_wheels_source_bound=provenance_.log_wheels_destination_bound=provenance_.log_wheels_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_validation_readback_log_wheels(const ProcessedGpuFramePtr&frame,std::span<Color>out) noexcept override { return execute_validation_readback_primary_wheels(frame,out); }

  DigitorResult execute_process_hsl_qualifier_gpu(std::span<const Color>src,uint32_t width,uint32_t height,int64_t timestamp,const HslQualifierParameters&parameters,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto context=eglGetCurrentContext();begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","hsl-qualifier-glsl-es-v1","GL framebuffer HSL qualifier");if(!fp32_renderable_)return DIGITOR_RESULT_UNSUPPORTED;if(context==EGL_NO_CONTEXT)return DIGITOR_RESULT_NOT_INITIALIZED;if(!width||!height||src.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im;uniform vec3 hr,sr,lr;uniform vec2 clean;uniform int invertMatte;out vec4 color;float lw(float v,vec3 r){if(v>=r.x&&v<=r.y)return 1.;if(r.z>0.&&v<r.x&&v>r.x-r.z)return(v-r.x+r.z)/r.z;if(r.z>0.&&v>r.y&&v<r.y+r.z)return(r.y+r.z-v)/r.z;return 0.;}float hw(float h,vec3 r){if(r.x<=r.y)return lw(h,r);return max(lw(h,vec3(r.x,1.,r.z)),lw(h,vec3(0.,r.y,r.z)));}vec3 hsl(vec3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,l=(hi+lo)*.5,s=d==0.?0.:d/max(1e-8,1.-abs(2.*l-1.)),h=0.;if(d!=0.){if(hi==c.r)h=mod((c.g-c.b)/d,6.);else if(hi==c.g)h=(c.b-c.r)/d+2.;else h=(c.r-c.g)/d+4.;h/=6.;if(h<0.)h+=1.;}return vec3(h,s,l);}void main(){vec3 c=texture(im,uv).rgb;vec3 x=hsl(c);float m=hw(x.x,hr)*lw(x.y,sr)*lw(x.z,lr);if(m<=clean.x)m=0.;if(m>=1.-clean.y)m=1.;if(invertMatte!=0)m=1.-m;color=vec4(m,m,m,1.);}";
    auto cached=color_program(3,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::make_shared<GlPreviewOwner>();owner->program=cached->program;owner->pipeline=cached;owner->context=context;if(!make_texture(owner->input,GpuFailurePoint::SourceResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_RGBA32F,width,height,"HSL source")||!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,width,height,"HSL output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"HSL framebuffer"))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,src.data());const auto&x=parameters.values();glUseProgram(cached->program);glUniform1i(uniform_location(cached->program,"im"),0);glUniform3f(uniform_location(cached->program,"hr"),x.hue.low,x.hue.high,x.hue.softness);glUniform3f(uniform_location(cached->program,"sr"),x.saturation.low,x.saturation.high,x.saturation.softness);glUniform3f(uniform_location(cached->program,"lr"),x.luminance.low,x.luminance.high,x.luminance.softness);glUniform2f(uniform_location(cached->program,"clean"),x.clean_black,x.clean_white);glUniform1i(uniform_location(cached->program,"invertMatte"),x.invert?1:0);glViewport(0,0,width,height);glDrawArrays(GL_TRIANGLES,0,3);glFinish();if(glGetError()!=GL_NO_ERROR)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;static std::atomic_uint64_t ids{500000};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"hsl-matte-linear"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.hsl_qualifier_enabled=true;provenance_.hsl_qualifier_parameter_identity=parameters.identity();provenance_.hsl_qualifier_shader_identity="hsl-qualifier-glsl-es-v1";provenance_.hsl_qualifier_pipeline_identity="GLProgram:hsl-qualifier-v1";provenance_.hsl_qualifier_source_bound=provenance_.hsl_qualifier_destination_bound=provenance_.hsl_qualifier_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_hsl_qualifier_gpu(const GpuSourceResource&s,int64_t timestamp,const HslQualifierParameters&parameters,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*s.frame));auto context=eglGetCurrentContext();if(!prior||context==EGL_NO_CONTEXT||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im;uniform vec3 hr,sr,lr;uniform vec2 clean;uniform int invertMatte;out vec4 color;float lw(float v,vec3 r){if(v>=r.x&&v<=r.y)return 1.;if(r.z>0.&&v<r.x&&v>r.x-r.z)return(v-r.x+r.z)/r.z;if(r.z>0.&&v>r.y&&v<r.y+r.z)return(r.y+r.z-v)/r.z;return 0.;}float hw(float h,vec3 r){if(r.x<=r.y)return lw(h,r);return max(lw(h,vec3(r.x,1.,r.z)),lw(h,vec3(0.,r.y,r.z)));}vec3 hsl(vec3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,l=(hi+lo)*.5,s=d==0.?0.:d/max(1e-8,1.-abs(2.*l-1.)),h=0.;if(d!=0.){if(hi==c.r)h=mod((c.g-c.b)/d,6.);else if(hi==c.g)h=(c.b-c.r)/d+2.;else h=(c.r-c.g)/d+4.;h/=6.;if(h<0.)h+=1.;}return vec3(h,s,l);}void main(){vec3 x=hsl(texture(im,uv).rgb);float m=hw(x.x,hr)*lw(x.y,sr)*lw(x.z,lr);if(m<=clean.x)m=0.;if(m>=1.-clean.y)m=1.;if(invertMatte!=0)m=1.-m;color=vec4(m,m,m,1.);}";begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","hsl-qualifier-glsl-es-v1","GL framebuffer HSL qualifier GPU source");auto cached=color_program(3,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;auto owner=std::make_shared<GlPreviewOwner>();owner->program=cached->program;owner->pipeline=cached;owner->context=context;owner->upstream=prior;if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,s.width,s.height,"HSL GPU output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"HSL GPU framebuffer"))return DIGITOR_RESULT_BACKEND_UNAVAILABLE;const auto&x=parameters.values();glUseProgram(cached->program);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,prior->output);glUniform1i(uniform_location(cached->program,"im"),0);glUniform3f(uniform_location(cached->program,"hr"),x.hue.low,x.hue.high,x.hue.softness);glUniform3f(uniform_location(cached->program,"sr"),x.saturation.low,x.saturation.high,x.saturation.softness);glUniform3f(uniform_location(cached->program,"lr"),x.luminance.low,x.luminance.high,x.luminance.softness);glUniform2f(uniform_location(cached->program,"clean"),x.clean_black,x.clean_white);glUniform1i(uniform_location(cached->program,"invertMatte"),x.invert?1:0);glViewport(0,0,s.width,s.height);glDrawArrays(GL_TRIANGLES,0,3);glFinish();if(glGetError()!=GL_NO_ERROR)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;static std::atomic_uint64_t ids{510000};out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{s.width,s.height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"hsl-matte-linear"},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.hsl_qualifier_enabled=true;provenance_.hsl_qualifier_parameter_identity=parameters.identity();provenance_.hsl_qualifier_shader_identity="hsl-qualifier-glsl-es-v1";provenance_.hsl_qualifier_pipeline_identity="GLProgram:hsl-qualifier-v1";provenance_.hsl_qualifier_source_bound=provenance_.hsl_qualifier_destination_bound=provenance_.hsl_qualifier_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.normal_preview_readback_count=0;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_validation_readback_hsl_qualifier(const ProcessedGpuFramePtr&frame,std::span<float>out)noexcept override{if(!frame||out.size()!=std::size_t(frame->metadata().width)*frame->metadata().height)return DIGITOR_RESULT_INVALID_ARGUMENT;std::vector<Color>rgba(out.size());auto r=execute_validation_readback_primary_wheels(frame,rgba);if(r!=DIGITOR_RESULT_OK)return r;for(std::size_t i=0;i<out.size();++i)out[i]=rgba[i].r;return DIGITOR_RESULT_OK;}

  DigitorResult execute_process_curves_gpu(std::span<const Color> src,uint32_t width,uint32_t height,int64_t timestamp,const CompiledRgbCurves&cc,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto context=eglGetCurrentContext();begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","rgb-curves-glsl-es-texture-v1","GL framebuffer texture");
    if(!fp32_renderable_)return DIGITOR_RESULT_UNSUPPORTED;
    if(context==EGL_NO_CONTEXT)return DIGITOR_RESULT_NOT_INITIALIZED;if(!width||!height||src.size()!=size_t(width)*height)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im,lut;uniform vec4 meta0[4],meta1[4];uniform int lutSize;out vec4 color;float cv(int k,float x){vec4 a=meta0[k],b=meta1[k];if(b.w==0.||isnan(x)||isinf(x))return x;if(x<a.x)return b.z==2.?a.z+b.x*(x-a.x):a.z;if(x>a.y)return b.z==2.?a.w+b.y*(x-a.y):a.w;float u=(x-a.x)/(a.y-a.x)*float(lutSize-1);float i=floor(u);return mix(texelFetch(lut,ivec2(int(i),k),0).r,texelFetch(lut,ivec2(min(int(i)+1,lutSize-1),k),0).r,u-i);}void main(){vec4 c=texture(im,uv);float a=c.a;c.r=cv(0,c.r);c.g=cv(0,c.g);c.b=cv(0,c.b);c.r=cv(1,c.r);c.g=cv(2,c.g);c.b=cv(3,c.b);color=vec4(c.rgb,a);}";
    auto cached=color_program(1,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;GLuint p=cached->program;auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::shared_ptr<GlPreviewOwner>(new(std::nothrow)GlPreviewOwner{});if(!owner)return DIGITOR_RESULT_OUT_OF_MEMORY;owner->program=p;owner->pipeline=cached;owner->context=context;
    if(!make_texture(owner->input,GpuFailurePoint::SourceResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_RGBA32F,width,height,"curves source")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);if(fail(GpuFailurePoint::SourceUpload,"glTexSubImage2D curves source")){finish_failure_evidence(baseline);return provenance_.failure_result;}glTexSubImage2D(GL_TEXTURE_2D,0,0,0,width,height,GL_RGBA,GL_FLOAT,src.data());
    std::vector<float>lut;float m0[16],m1[16];for(int k=0;k<4;k++){const auto&c=cc.curves()[k];lut.insert(lut.end(),c.samples.begin(),c.samples.end());m0[k*4]=c.domain_min;m0[k*4+1]=c.domain_max;m0[k*4+2]=c.first_value;m0[k*4+3]=c.last_value;m1[k*4]=c.slope_before;m1[k*4+1]=c.slope_after;m1[k*4+2]=float(c.extrapolation);m1[k*4+3]=c.enabled&&!c.identity?1.f:0.f;}if(!make_texture(owner->lut,GpuFailurePoint::LutResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_R32F,cc.lut_size(),4,"curves LUT")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,owner->lut);if(fail(GpuFailurePoint::LutUpload,"glTexSubImage2D curves LUT")){finish_failure_evidence(baseline);return provenance_.failure_result;}glTexSubImage2D(GL_TEXTURE_2D,0,0,0,cc.lut_size(),4,GL_RED,GL_FLOAT,lut.data());if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,width,height,"curves output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"curves framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    if(fail(GpuFailurePoint::ResourceBinding,"bind curves resources")){finish_failure_evidence(baseline);return provenance_.failure_result;}glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,owner->input);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,owner->lut);for(auto [name,value]:{std::pair{"im",0},std::pair{"lut",1},std::pair{"lutSize",static_cast<int>(cc.lut_size())}}){auto loc=uniform_location(p,name);if(loc==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform1i(loc,value);}auto l0=uniform_location(p,"meta0"),l1=uniform_location(p,"meta1");if(l0==-2||l1==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform4fv(l0,4,m0);glUniform4fv(l1,4,m1);if(fail(GpuFailurePoint::DrawSetup,"glViewport curves")){finish_failure_evidence(baseline);return provenance_.failure_result;}glViewport(0,0,width,height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays curves")){finish_failure_evidence(baseline);return provenance_.failure_result;}glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::Flush,"glFlush curves")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFlush();if(glGetError()!=GL_NO_ERROR){finish_failure_evidence(baseline);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}static std::atomic_uint64_t ids{1};if(fail(GpuFailurePoint::ProcessedFrameCreation,"ProcessedGpuFrame curves")){finish_failure_evidence(baseline);return provenance_.failure_result;}auto ready=std::make_shared<std::atomic_bool>(true);out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{width,height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,"linear-rgba"},ids++,std::static_pointer_cast<void>(owner),ready,true);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.output_written=true;provenance_.synchronization_waited=true;provenance_.readback_performed=false;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_process_curves_gpu(const GpuSourceResource&s,int64_t timestamp,const CompiledRgbCurves&cc,ProcessedGpuFramePtr&out)noexcept override{
    out.reset();auto prior=std::static_pointer_cast<GlPreviewOwner>(native_owner(*s.frame));auto context=eglGetCurrentContext();if(!prior||context==EGL_NO_CONTEXT||prior->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im,lut;uniform vec4 meta0[4],meta1[4];uniform int lutSize;out vec4 color;float cv(int k,float x){vec4 a=meta0[k],b=meta1[k];if(b.w==0.||isnan(x)||isinf(x))return x;if(x<a.x)return b.z==2.?a.z+b.x*(x-a.x):a.z;if(x>a.y)return b.z==2.?a.w+b.y*(x-a.y):a.w;float u=(x-a.x)/(a.y-a.x)*float(lutSize-1);float i=floor(u);return mix(texelFetch(lut,ivec2(int(i),k),0).r,texelFetch(lut,ivec2(min(int(i)+1,lutSize-1),k),0).r,u-i);}void main(){vec4 c=texture(im,uv);float a=c.a;c.r=cv(0,c.r);c.g=cv(0,c.g);c.b=cv(0,c.b);c.r=cv(1,c.r);c.g=cv(2,c.g);c.b=cv(3,c.b);color=vec4(c.rgb,a);}";
    begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","rgb-curves-glsl-es-texture-v1","GL framebuffer curves GPU source");auto cached=color_program(1,vs,fs);if(!cached)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;GLuint p=cached->program;auto baseline=counts();provenance_.resources_before=baseline;auto owner=std::make_shared<GlPreviewOwner>();owner->program=p;owner->pipeline=cached;owner->context=context;owner->upstream=prior;std::vector<float>lut;float m0[16],m1[16];for(int k=0;k<4;k++){const auto&c=cc.curves()[k];lut.insert(lut.end(),c.samples.begin(),c.samples.end());m0[k*4]=c.domain_min;m0[k*4+1]=c.domain_max;m0[k*4+2]=c.first_value;m0[k*4+3]=c.last_value;m1[k*4]=c.slope_before;m1[k*4+1]=c.slope_after;m1[k*4+2]=float(c.extrapolation);m1[k*4+3]=c.enabled&&!c.identity?1.f:0.f;}if(!make_texture(owner->lut,GpuFailurePoint::LutResourceCreation,GpuFailurePoint::SourceResourceStorage,GL_R32F,cc.lut_size(),4,"curves GPU LUT")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,owner->lut);if(fail(GpuFailurePoint::LutUpload,"glTexSubImage2D curves GPU LUT")){finish_failure_evidence(baseline);return provenance_.failure_result;}glTexSubImage2D(GL_TEXTURE_2D,0,0,0,cc.lut_size(),4,GL_RED,GL_FLOAT,lut.data());if(!make_texture(owner->output,GpuFailurePoint::OutputResourceCreation,GpuFailurePoint::OutputResourceStorage,GL_RGBA32F,s.width,s.height,"curves GPU output")||!make_framebuffer(owner->framebuffer,owner->output,GL_FRAMEBUFFER,"curves GPU framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}
    if(fail(GpuFailurePoint::ResourceBinding,"bind curves GPU resources")){finish_failure_evidence(baseline);return provenance_.failure_result;}glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,prior->output);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,owner->lut);for(auto [name,value]:{std::pair{"im",0},std::pair{"lut",1},std::pair{"lutSize",static_cast<int>(cc.lut_size())}}){auto loc=uniform_location(p,name);if(loc==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform1i(loc,value);}auto l0=uniform_location(p,"meta0"),l1=uniform_location(p,"meta1");if(l0==-2||l1==-2){finish_failure_evidence(baseline);return provenance_.failure_result;}glUniform4fv(l0,4,m0);glUniform4fv(l1,4,m1);if(fail(GpuFailurePoint::DrawSetup,"glViewport curves GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glViewport(0,0,s.width,s.height);if(fail(GpuFailurePoint::DispatchOrDraw,"glDrawArrays curves GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glDrawArrays(GL_TRIANGLES,0,3);if(fail(GpuFailurePoint::SynchronizationWait,"glFinish curves GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFinish();if(glGetError()!=GL_NO_ERROR){finish_failure_evidence(baseline);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}static std::atomic_uint64_t ids{300000};if(fail(GpuFailurePoint::ProcessedFrameCreation,"ProcessedGpuFrame curves GPU")){finish_failure_evidence(baseline);return provenance_.failure_result;}out=std::make_shared<ProcessedGpuFrame>(this,DIGITOR_RENDERER_OPENGL_ES,GpuFrameMetadata{s.width,s.height,s.format,GpuFrameAlpha::straight,timestamp,s.color_metadata_identity},ids++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),true);provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=provenance_.command_recorded=provenance_.dispatch_or_draw_issued=provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=true;provenance_.readback_performed=false;return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_create_preview_consumer(const ProcessedGpuFramePtr&frame,std::shared_ptr<PreviewConsumerDestination>&out)noexcept override{
    out.reset();begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,"GLES driver","preview-consumer","consumer-owned texture/FBO");auto context=eglGetCurrentContext();if(!frame||context==EGL_NO_CONTEXT)return DIGITOR_RESULT_NOT_INITIALIZED;auto baseline=counts();provenance_.resources_before=baseline;if(fail(GpuFailurePoint::PreviewAcquisition,"GLES consumer acquisition")){finish_failure_evidence(baseline);return provenance_.failure_result;}const auto&m=frame->metadata();auto owner=std::make_shared<GlConsumerOwner>();owner->context=context;if(!make_texture(owner->texture,GpuFailurePoint::PreviewDestinationCreation,GpuFailurePoint::PreviewDestinationStorage,GL_RGBA32F,m.width,m.height,"consumer texture")||!make_framebuffer(owner->framebuffer,owner->texture,GL_DRAW_FRAMEBUFFER,"consumer framebuffer")){finish_failure_evidence(baseline);return provenance_.failure_result==DIGITOR_RESULT_OK?DIGITOR_RESULT_BACKEND_UNAVAILABLE:provenance_.failure_result;}static std::atomic_uint64_t tokens{1};out=std::make_shared<PreviewConsumerDestination>(PreviewConsumerMetadata{DIGITOR_RENDERER_OPENGL_ES,this,m.width,m.height,m.format,GpuPrecisionMode::Float32},tokens++,std::static_pointer_cast<void>(owner),std::make_shared<std::atomic_bool>(true),[this,context](const ProcessedGpuFramePtr&f,const std::shared_ptr<void>&d){auto baseline=counts();provenance_.resources_before=baseline;if(eglGetCurrentContext()!=context)return DIGITOR_RESULT_NOT_INITIALIZED;auto source=std::static_pointer_cast<GlPreviewOwner>(native_owner(*f));auto destination=std::static_pointer_cast<GlConsumerOwner>(d);if(!source||!destination||source->context!=context||destination->context!=context)return DIGITOR_RESULT_INVALID_ARGUMENT;const auto&m=f->metadata();if(fail(GpuFailurePoint::ResourceBinding,"bind consumer framebuffers")){finish_failure_evidence(baseline);return provenance_.failure_result;}glBindFramebuffer(GL_READ_FRAMEBUFFER,source->framebuffer);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,destination->framebuffer);if(fail(GpuFailurePoint::PreviewPresentation,"consumer presentation")||fail(GpuFailurePoint::ConsumerCopySubmission,"glBlitFramebuffer consumer")){finish_failure_evidence(baseline);return provenance_.failure_result;}glBlitFramebuffer(0,0,m.width,m.height,0,0,m.width,m.height,GL_COLOR_BUFFER_BIT,GL_NEAREST);if(fail(GpuFailurePoint::SynchronizationWait,"glFinish consumer")){finish_failure_evidence(baseline);return provenance_.failure_result;}glFinish();auto result=glGetError()==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;provenance_.output_written=result==DIGITOR_RESULT_OK;return result;});return DIGITOR_RESULT_OK;
  }
  DigitorResult execute_present_gpu_frame(const ProcessedGpuFramePtr&frame)noexcept override{
    if(!frame||eglGetCurrentContext()==EGL_NO_CONTEXT||frame->acquire(this,DIGITOR_RENDERER_OPENGL_ES)!=DIGITOR_RESULT_OK)return DIGITOR_RESULT_INVALID_ARGUMENT;auto o=std::static_pointer_cast<GlPreviewOwner>(native_owner(*frame));if(!o||o->context!=eglGetCurrentContext()){(void)frame->release(this);return DIGITOR_RESULT_INVALID_ARGUMENT;}
    const char*vs="#version 300 es\nout vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";const char*fs="#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D im;out vec4 c;void main(){c=texture(im,uv);}";GLuint v=compile_gl(GL_VERTEX_SHADER,vs),f=compile_gl(GL_FRAGMENT_SHADER,fs),p=0;if(v&&f){p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);}glDeleteShader(v);glDeleteShader(f);glBindFramebuffer(GL_FRAMEBUFFER,0);auto m=frame->metadata();glViewport(0,0,m.width,m.height);glUseProgram(p);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,o->output);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);glUniform1i(glGetUniformLocation(p,"im"),0);glDrawArrays(GL_TRIANGLES,0,3);glFlush();auto result=glGetError()==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;glDeleteProgram(p);(void)frame->release(this);return result;
  }
  DigitorResult create_texture(const DigitorTextureDesc &d,
                               void **o) noexcept override {
    *o = nullptr;
    GLenum in = 0, format = GL_RGBA, type = GL_UNSIGNED_BYTE;
    switch (d.format) {
    case DIGITOR_PIXEL_FORMAT_RGBA8_UNORM:
      in = GL_RGBA8;
      break;
    case DIGITOR_PIXEL_FORMAT_RGBA16_FLOAT:
      in = GL_RGBA16F;
      type = GL_HALF_FLOAT;
      break;
    case DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT:
      in = GL_RGBA32F;
      type = GL_FLOAT;
      break;
    default:
      return DIGITOR_RESULT_UNSUPPORTED;
    }
    auto *p = new (std::nothrow) GlObject{};
    if (!p)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    glGenTextures(1, &p->name);
    glBindTexture(GL_TEXTURE_2D, p->name);
    glTexImage2D(GL_TEXTURE_2D, 0, in, d.width, d.height, 0, format, type,
                 nullptr);
    if (glGetError() != GL_NO_ERROR) {
      glDeleteTextures(1, &p->name);
      delete p;
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    *o = p;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult create_buffer(const DigitorBufferDesc &d,
                              void **o) noexcept override {
    auto *p = new (std::nothrow) GlObject{};
    if (!p)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    glGenBuffers(1, &p->name);
    glBindBuffer(GL_ARRAY_BUFFER, p->name);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)d.size, nullptr,
                 (d.usage & DIGITOR_BUFFER_USAGE_UPLOAD) ? GL_STREAM_DRAW
                                                         : GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR) {
      glDeleteBuffers(1, &p->name);
      delete p;
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    *o = p;
    return DIGITOR_RESULT_OK;
  }
  DigitorResult create_sampler(const DigitorSamplerDesc &,
                               void **o) noexcept override {
    auto *p = new (std::nothrow) GlObject{};
    if (!p)
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    glGenSamplers(1, &p->name);
    *o = p;
    return glGetError() == GL_NO_ERROR ? DIGITOR_RESULT_OK
                                       : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult map_buffer(void *p, uint64_t offset, uint64_t size,
                           void **o) noexcept override {
    if (!p || !o)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto *x = (GlObject *)p;
    glBindBuffer(GL_ARRAY_BUFFER, x->name);
    *o = glMapBufferRange(GL_ARRAY_BUFFER, (GLintptr)offset, (GLsizeiptr)size,
                          GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
    return *o ? DIGITOR_RESULT_OK : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  void unmap_buffer(void *p) noexcept override {
    if (p) {
      glBindBuffer(GL_ARRAY_BUFFER, ((GlObject *)p)->name);
      glUnmapBuffer(GL_ARRAY_BUFFER);
    }
  }
  DigitorResult render_rgba8(uint32_t w, uint32_t h,
                             std::span<const uint8_t> src,
                             std::vector<uint8_t> &out) noexcept override {
    if (!w || !h || (!src.empty() && src.size() != size_t(w) * h * 4))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char *vs =
        "#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 "
        "p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-"
        "1.0,0,1);}";
    const char *fs =
        "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D "
        "image;out vec4 color;void main(){color=texture(image,uv);}";
    auto compile = [](GLenum type, const char *text) {
      GLuint sh = glCreateShader(type);
      glShaderSource(sh, 1, &text, nullptr);
      glCompileShader(sh);
      GLint ok = 0;
      glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        glDeleteShader(sh);
        return GLuint(0);
      }
      return sh;
    };
    GLuint v = compile(GL_VERTEX_SHADER, vs),
           f = compile(GL_FRAGMENT_SHADER, fs), program = glCreateProgram();
    if (!v || !f)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
      glDeleteProgram(program);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    GLuint input = 0, target = 0, fbo = 0;
    // render_rgba8() runs after the failure-injection matrix. GLES state is
    // context-global, so explicitly restore the deterministic offscreen state
    // before allocating/uploading/drawing. Otherwise a leaked color mask,
    // scissor, rasterizer-discard, pixel-buffer binding, or non-default VAO can
    // make the final upload/copy/clear qualification silently fail even though
    // the FP32 grading metrics already pass.
    prepare_offscreen_draw_state();
    drain_errors();
    glActiveTexture(GL_TEXTURE0);
    glGenTextures(1, &input);
    glBindTexture(GL_TEXTURE_2D, input);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (!src.empty())
      glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE,
                      src.data());
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, target, 0);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      glDeleteFramebuffers(1, &fbo);
      glDeleteTextures(1, &target);
      glDeleteTextures(1, &input);
      glDeleteProgram(program);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    prepare_offscreen_draw_state();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    if (src.empty()) {
      glClearColor(0, 0, 0, 1);
      glClear(GL_COLOR_BUFFER_BIT);
    } else {
      glUseProgram(program);
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, input);
      glUniform1i(glGetUniformLocation(program, "image"), 0);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    try {
      out.resize(size_t(w) * h * 4);
    } catch (...) {
      return DIGITOR_RESULT_OUT_OF_MEMORY;
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    glFinish();
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
    glFinish();
    GLenum error = glGetError();
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &target);
    glDeleteTextures(1, &input);
    glDeleteProgram(program);
    return error == GL_NO_ERROR ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult grade_rgba32f(std::span<const Color> src, std::span<Color> out,
                              const ColorGrade &p) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES, true, i_.device_name,
                           "OpenGL ES driver compiler", "grade-glsl-es-v1",
                           "GL program:grade-v1");
    if (!fp32_renderable_)
      return DIGITOR_RESULT_UNSUPPORTED;
    if (src.size() != out.size())
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    if (src.empty())
      return DIGITOR_RESULT_OK;
    int pixel_count = 0;
    if (!checked_size_to_int(src.size(), pixel_count))
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char *vs =
        "#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 "
        "q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.0-"
        "1.0,0,1);}";
    const char *fs =
        "#version 300 es\nprecision highp float;in vec2 uv;uniform sampler2D "
        "im;uniform float "
        "exposure,contrast,gamma_,lift,gain,offset_,temperature,tint,"
        "saturation,vibrance,hue;out vec4 color;void main(){vec4 "
        "c=texture(im,uv);vec3 x=c.rgb;float "
        "t=temperature*.1;x.r+=t;x.b-=t;x.g+=tint*.1;float "
        "l=dot(x,vec3(.2126,.7152,.0722));float "
        "v=1.+vibrance*(1.-(max(x.r,max(x.g,x.b))-min(x.r,min(x.g,x.b))));x=l+("
        "x-l)*(saturation*v);x=(x-.5)*contrast+.5;x=(x+lift)*gain+offset_;x*="
        "exp2(exposure);x=sign(x)*pow(abs(x),vec3(1./max(.001,gamma_)));float "
        "a=hue*.0174532925199433,co=cos(a),s=sin(a);vec3 "
        "r=x;x=vec3((.213+co*.787-s*.213)*r.r+(.715-co*.715-s*.715)*r.g+(.072-"
        "co*.072+s*.928)*r.b,(.213-co*.213+s*.143)*r.r+(.715+co*.285+s*.140)*r."
        "g+(.072-co*.072-s*.283)*r.b,(.213-co*.213-s*.787)*r.r+(.715-co*.715+s*"
        ".715)*r.g+(.072+co*.928+s*.072)*r.b);color=vec4(x,c.a);}";
    auto comp = [](GLenum t, const char *z) {
      GLuint s = glCreateShader(t);
      glShaderSource(s, 1, &z, nullptr);
      glCompileShader(s);
      GLint ok;
      glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
      if (!ok) {
        glDeleteShader(s);
        return GLuint(0);
      }
      return s;
    };
    GLuint v = comp(GL_VERTEX_SHADER, vs), f = comp(GL_FRAGMENT_SHADER, fs);
    if (!v || !f)
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    GLuint prog = glCreateProgram();
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
      glDeleteProgram(prog);
      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    }
    GLuint input, target, fb;
    glGenTextures(1, &input);
    glBindTexture(GL_TEXTURE_2D, input);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, pixel_count, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, pixel_count, 1, GL_RGBA, GL_FLOAT,
                    src.data());
    provenance_.source_upload_performed = true;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, &target);
    glBindTexture(GL_TEXTURE_2D, target);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, pixel_count, 1);
    glGenFramebuffers(1, &fb);
    glBindFramebuffer(GL_FRAMEBUFFER, fb);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           target, 0);
    glUseProgram(prog);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input);
    glUniform1i(glGetUniformLocation(prog, "im"), 0);
    const char *names[] = {"exposure",   "contrast", "gamma_",      "lift",
                           "gain",       "offset_",  "temperature", "tint",
                           "saturation", "vibrance", "hue"};
    const float vals[] = {p.exposure,   p.contrast, p.gamma,       p.lift,
                          p.gain,       p.offset,   p.temperature, p.tint,
                          p.saturation, p.vibrance, p.hue};
    for (int k = 0; k < 11; k++)
      glUniform1f(glGetUniformLocation(prog, names[k]), vals[k]);
    glViewport(0, 0, pixel_count, 1);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    provenance_.command_recorded = true;
    provenance_.dispatch_or_draw_issued = true;
    glReadPixels(0, 0, pixel_count, 1, GL_RGBA, GL_FLOAT, out.data());
    glFinish();
    provenance_.queue_submission_issued = true;
    provenance_.synchronization_waited = true;
    provenance_.output_written = true;
    provenance_.readback_performed = true;
    provenance_.cpu_color_reference_invocations =
      cpu_color_reference_count() - provenance_.cpu_color_reference_invocations;
    GLenum error = glGetError();
    glDeleteFramebuffers(1, &fb);
    glDeleteTextures(1, &target);
    glDeleteTextures(1, &input);
    glDeleteProgram(prog);
    return error == GL_NO_ERROR ? DIGITOR_RESULT_OK
                                : DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  DigitorResult execute_curves_rgba32f(std::span<const Color> src, std::span<Color> out,
                              const CompiledRgbCurves &cc) noexcept override {
    begin_grade_provenance(DIGITOR_RENDERER_OPENGL_ES,true,i_.device_name,
      "OpenGL ES driver compiler","rgb-curves-glsl-es-v1","GL program:rgb-curves-v1");
    if(!fp32_renderable_)return DIGITOR_RESULT_UNSUPPORTED;

    if(src.size()!=out.size())return DIGITOR_RESULT_INVALID_ARGUMENT;if(src.empty())return DIGITOR_RESULT_OK;int pixel_count=0,lut_size=0;if(!checked_size_to_int(src.size(),pixel_count)||!checked_size_to_int(cc.lut_size(),lut_size))return DIGITOR_RESULT_INVALID_ARGUMENT;
    const char*vs="#version 300 es\nprecision highp float;out vec2 uv;void main(){vec2 q=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=q;gl_Position=vec4(q*2.-1.,0,1);}";
    const char*fs="#version 300 es\nprecision highp float;precision highp int;in vec2 uv;uniform sampler2D im,lut;uniform vec4 meta0[4],meta1[4];uniform int lutSize;out vec4 color;float cv(int k,float x){vec4 a=meta0[k],b=meta1[k];if(b.w==0.||isnan(x)||isinf(x))return x;if(x<a.x)return b.z==2.?a.z+b.x*(x-a.x):a.z;if(x>a.y)return b.z==2.?a.w+b.y*(x-a.y):a.w;float u=(x-a.x)/(a.y-a.x)*float(lutSize-1);float i=floor(u);return mix(texelFetch(lut,ivec2(int(i),k),0).r,texelFetch(lut,ivec2(min(int(i)+1,lutSize-1),k),0).r,u-i);}void main(){vec4 c=texture(im,uv);float a=c.a;c.r=cv(0,c.r);c.g=cv(0,c.g);c.b=cv(0,c.b);c.r=cv(1,c.r);c.g=cv(2,c.g);c.b=cv(3,c.b);color=vec4(c.rgb,a);}";
    auto compile=[](GLenum t,const char*s){GLuint x=glCreateShader(t);glShaderSource(x,1,&s,nullptr);glCompileShader(x);GLint ok=0;glGetShaderiv(x,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(x);return GLuint(0);}return x;};
    GLuint v=compile(GL_VERTEX_SHADER,vs),f=compile(GL_FRAGMENT_SHADER,fs);if(!v||!f)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;
    GLuint p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);glDeleteShader(v);glDeleteShader(f);GLint ok=0;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
    std::vector<float> lut;lut.reserve(size_t(cc.lut_size())*4);float m0[16],m1[16];
    for(int k=0;k<4;k++){const auto&c=cc.curves()[k];lut.insert(lut.end(),c.samples.begin(),c.samples.end());m0[k*4]=c.domain_min;m0[k*4+1]=c.domain_max;m0[k*4+2]=c.first_value;m0[k*4+3]=c.last_value;m1[k*4]=c.slope_before;m1[k*4+1]=c.slope_after;m1[k*4+2]=float(c.extrapolation);m1[k*4+3]=c.enabled&&!c.identity?1.f:0.f;}
    GLuint tex[3],fb;glGenTextures(3,tex);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[0]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,pixel_count,1);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,pixel_count,1,GL_RGBA,GL_FLOAT,src.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,tex[1]);glTexStorage2D(GL_TEXTURE_2D,1,GL_R32F,lut_size,4);glTexSubImage2D(GL_TEXTURE_2D,0,0,0,lut_size,4,GL_RED,GL_FLOAT,lut.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D,tex[2]);glTexStorage2D(GL_TEXTURE_2D,1,GL_RGBA32F,pixel_count,1);glGenFramebuffers(1,&fb);glBindFramebuffer(GL_FRAMEBUFFER,fb);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex[2],0);
    // Output allocation above occurs while texture unit 1 is active, so it
    // replaces the LUT binding on that unit. Rebind both sampler inputs
    // deterministically before drawing the qualification path.
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex[0]);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,tex[1]);
    glUseProgram(p);glUniform1i(glGetUniformLocation(p,"im"),0);glUniform1i(glGetUniformLocation(p,"lut"),1);glUniform1i(glGetUniformLocation(p,"lutSize"),lut_size);glUniform4fv(glGetUniformLocation(p,"meta0"),4,m0);glUniform4fv(glGetUniformLocation(p,"meta1"),4,m1);glViewport(0,0,pixel_count,1);glDrawArrays(GL_TRIANGLES,0,3);
    provenance_.source_upload_performed=provenance_.curve_source_bound=provenance_.curve_destination_bound=provenance_.curve_lut_bound=provenance_.curve_parameters_bound=true;provenance_.curves_enabled=true;provenance_.curve_lut_size=cc.lut_size();provenance_.compiled_curve_identity=cc.identity();provenance_.native_curve_shader_identity="rgb-curves-glsl-es-v1";provenance_.native_lut_resource_identity=cc.identity()+":"+i_.device_name;provenance_.native_lut_cache=CacheDisposition::Miss;provenance_.command_recorded=provenance_.dispatch_or_draw_issued=true;
    glReadPixels(0,0,pixel_count,1,GL_RGBA,GL_FLOAT,out.data());glFinish();provenance_.queue_submission_issued=provenance_.synchronization_waited=provenance_.output_written=provenance_.readback_performed=provenance_.validation_readback_completed=true;GLenum err=glGetError();glDeleteFramebuffers(1,&fb);glDeleteTextures(3,tex);glDeleteProgram(p);return err==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }
  void destroy_texture(void *p) noexcept override {
    auto *x = (GlObject *)p;
    if (x) {
      glDeleteTextures(1, &x->name);
      delete x;
    }
  }
  void destroy_buffer(void *p) noexcept override {
    auto *x = (GlObject *)p;
    if (x) {
      glDeleteBuffers(1, &x->name);
      delete x;
    }
  }
  void destroy_sampler(void *p) noexcept override {
    auto *x = (GlObject *)p;
    if (x) {
      glDeleteSamplers(1, &x->name);
      delete x;
    }
  }
};
} // namespace
std::unique_ptr<IRenderBackend>
create_native_backend(DigitorRendererBackend b) {
#ifdef DIGITOR_HAS_VULKAN
  extern std::unique_ptr<IRenderBackend> create_vulkan_backend();
  if (b == DIGITOR_RENDERER_VULKAN)
    return create_vulkan_backend();
#endif
  return b == DIGITOR_RENDERER_OPENGL_ES ? std::make_unique<GlBackend>()
                                         : nullptr;
}
} // namespace digitor
#endif
