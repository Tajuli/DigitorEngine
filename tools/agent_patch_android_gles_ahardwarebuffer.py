from pathlib import Path

path = Path('src/gpu/gles_backend.cpp')
text = path.read_text()

old = '''#include <GLES3/gl31.h>\n#include <EGL/egl.h>\n#include <cstring>\n#include <atomic>\n#include <new>\n'''
new = '''#include <GLES3/gl31.h>\n#include <GLES2/gl2ext.h>\n#include <EGL/egl.h>\n#include <EGL/eglext.h>\n#include <android/hardware_buffer.h>\n#include <dlfcn.h>\n#include <unistd.h>\n#include <cstring>\n#include <atomic>\n#include <new>\n'''
assert old in text
text = text.replace(old, new, 1)

anchor = '''struct GlConsumerOwner { GLuint texture{},framebuffer{};EGLContext context{};\n GlConsumerOwner(){++gl_live.consumers;}\n ~GlConsumerOwner(){delete_framebuffer(framebuffer,context);delete_texture(texture,context);--gl_live.consumers;}\n};\nbool has_gl_extension(const char* required) noexcept {\n'''
insert = '''struct GlConsumerOwner { GLuint texture{},framebuffer{};EGLContext context{};\n GlConsumerOwner(){++gl_live.consumers;}\n ~GlConsumerOwner(){delete_framebuffer(framebuffer,context);delete_texture(texture,context);--gl_live.consumers;}\n};\n\nusing AHardwareBufferDescribeFn =\n    void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);\nusing EglGetNativeClientBufferAndroidFn =\n    EGLClientBuffer (*)(const AHardwareBuffer*);\nusing EglCreateImageKhrFn = EGLImageKHR (*)(\n    EGLDisplay, EGLContext, EGLenum, EGLClientBuffer, const EGLint*);\nusing EglDestroyImageKhrFn = EGLBoolean (*)(EGLDisplay, EGLImageKHR);\nusing GlEglImageTargetTexture2DOesFn = void (*)(GLenum, GLeglImageOES);\nusing EglCreateSyncKhrFn =\n    EGLSyncKHR (*)(EGLDisplay, EGLenum, const EGLint*);\nusing EglWaitSyncKhrFn = EGLBoolean (*)(EGLDisplay, EGLSyncKHR, EGLint);\nusing EglDestroySyncKhrFn = EGLBoolean (*)(EGLDisplay, EGLSyncKHR);\n\ntemplate <class T>\nT resolve_egl_proc(const char* name) noexcept {\n  return reinterpret_cast<T>(eglGetProcAddress(name));\n}\n\nAHardwareBufferDescribeFn resolve_ahardwarebuffer_describe() noexcept {\n  static const auto fn = reinterpret_cast<AHardwareBufferDescribeFn>(\n      dlsym(RTLD_DEFAULT, "AHardwareBuffer_describe"));\n  return fn;\n}\n\nbool has_egl_extension(EGLDisplay display, const char* required) noexcept {\n  if (display == EGL_NO_DISPLAY || required == nullptr || *required == '\\0')\n    return false;\n  const char* extensions = eglQueryString(display, EGL_EXTENSIONS);\n  if (!extensions) return false;\n  const auto length = std::strlen(required);\n  const char* cursor = extensions;\n  while ((cursor = std::strstr(cursor, required)) != nullptr) {\n    const bool left = cursor == extensions || cursor[-1] == ' ';\n    const bool right = cursor[length] == '\\0' || cursor[length] == ' ';\n    if (left && right) return true;\n    cursor += length;\n  }\n  return false;\n}\n\nstruct GlImportedSurfaceOwner {\n  GLuint texture{};\n  EGLImageKHR image{EGL_NO_IMAGE_KHR};\n  EGLDisplay display{EGL_NO_DISPLAY};\n  EGLContext context{EGL_NO_CONTEXT};\n  EglDestroyImageKhrFn destroy_image{};\n  NativeMediaSurfacePtr surface;\n  ~GlImportedSurfaceOwner() {\n    delete_texture(texture, context);\n    if (image != EGL_NO_IMAGE_KHR && display != EGL_NO_DISPLAY &&\n        destroy_image != nullptr && eglGetCurrentContext() == context) {\n      (void)destroy_image(display, image);\n      image = EGL_NO_IMAGE_KHR;\n    }\n  }\n};\n\nbool has_gl_extension(const char* required) noexcept {\n'''
assert anchor in text
text = text.replace(anchor, insert, 1)

anchor = '''  bool owns_egl_context_{};\n\n  bool make_context_current() noexcept {\n'''
replace = '''  bool owns_egl_context_{};\n  bool native_media_import_ready_{};\n\n  bool make_context_current() noexcept {\n'''
assert anchor in text
text = text.replace(anchor, replace, 1)

anchor = '''  DigitorRendererInfo i_{};\n  bool fp32_renderable_{};\n'''
program = r'''  std::shared_ptr<GlPipelineOwner> android_import_program() noexcept {
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
'''
assert anchor in text
text = text.replace(anchor, program, 1)

anchor = '''public:\n  BackendProductionCapability production_capability() const noexcept override {\n'''
importer = r'''  DigitorResult import_android_ahardwarebuffer(
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
'''
assert anchor in text
text = text.replace(anchor, importer, 1)

old = '''    out.resources = GlesProductionResources{\n        reinterpret_cast<void*>(display_), reinterpret_cast<void*>(context_)};\n    return out;\n'''
new = '''    out.resources = GlesProductionResources{\n        reinterpret_cast<void*>(display_), reinterpret_cast<void*>(context_)};\n    if (native_media_import_ready_ && display_ != EGL_NO_DISPLAY &&\n        context_ != EGL_NO_CONTEXT) {\n      out.native_media_import =\n          [self = const_cast<GlBackend*>(this)](\n              const ZeroCopyImportRequest& request,\n              ProcessedGpuFramePtr& frame) noexcept {\n            return self->import_android_ahardwarebuffer(request, frame);\n          };\n    }\n    return out;\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''    glGenVertexArrays(1, &fullscreen_vao_);\n    if (fullscreen_vao_ != 0) glBindVertexArray(fullscreen_vao_);\n    i_.supports_fp32 = fp32_renderable_ ? 1 : 0;\n'''
new = '''    glGenVertexArrays(1, &fullscreen_vao_);\n    if (fullscreen_vao_ != 0) glBindVertexArray(fullscreen_vao_);\n    native_media_import_ready_ =\n        fp32_renderable_ && resolve_ahardwarebuffer_describe() != nullptr &&\n        has_gl_extension("GL_OES_EGL_image_external_essl3") &&\n        has_egl_extension(display_, "EGL_KHR_image_base") &&\n        has_egl_extension(display_, "EGL_ANDROID_image_native_buffer") &&\n        has_egl_extension(display_, "EGL_ANDROID_get_native_client_buffer") &&\n        has_egl_extension(display_, "EGL_ANDROID_native_fence_sync") &&\n        has_egl_extension(display_, "EGL_KHR_wait_sync") &&\n        resolve_egl_proc<EglGetNativeClientBufferAndroidFn>(\n            "eglGetNativeClientBufferANDROID") != nullptr &&\n        resolve_egl_proc<EglCreateImageKhrFn>("eglCreateImageKHR") != nullptr &&\n        resolve_egl_proc<EglDestroyImageKhrFn>("eglDestroyImageKHR") != nullptr &&\n        resolve_egl_proc<GlEglImageTargetTexture2DOesFn>(\n            "glEGLImageTargetTexture2DOES") != nullptr &&\n        resolve_egl_proc<EglCreateSyncKhrFn>("eglCreateSyncKHR") != nullptr &&\n        resolve_egl_proc<EglWaitSyncKhrFn>("eglWaitSyncKHR") != nullptr &&\n        resolve_egl_proc<EglDestroySyncKhrFn>("eglDestroySyncKHR") != nullptr;\n    i_.supports_fp32 = fp32_renderable_ ? 1 : 0;\n'''
assert old in text
text = text.replace(old, new, 1)

old = '''    display_ = EGL_NO_DISPLAY;\n    context_ = EGL_NO_CONTEXT;\n    surface_ = EGL_NO_SURFACE;\n    owns_egl_context_ = false;\n'''
new = '''    native_media_import_ready_ = false;\n    display_ = EGL_NO_DISPLAY;\n    context_ = EGL_NO_CONTEXT;\n    surface_ = EGL_NO_SURFACE;\n    owns_egl_context_ = false;\n'''
assert old in text
text = text.replace(old, new, 1)

path.write_text(text)
print('patched', path)
