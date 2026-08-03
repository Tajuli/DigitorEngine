#include "digitor/android_gles_effect_provider.hpp"

#if defined(__ANDROID__)

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace digitor {
namespace {

constexpr const char* kVertexShader = R"GLSL(#version 300 es
precision highp float;
const vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));
out vec2 uv;
void main(){ gl_Position=vec4(p[gl_VertexID],0.0,1.0); uv=p[gl_VertexID]*0.5+0.5; }
)GLSL";

constexpr const char* kFragmentShader = R"GLSL(#version 300 es
precision highp float;
precision highp int;
in vec2 uv;
layout(location=0) out vec4 outColor;
uniform sampler2D srcTex;
uniform int effectKind;
uniform int passIndex;
uniform int quality;
uniform float amount;
uniform float radius;
uniform float angle;
uniform vec2 texel;
uniform uvec2 seed;
float hash21(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7))+float(seed.x^seed.y))*43758.5453); }
vec4 px(vec2 o){ return texture(srcTex, clamp(uv+o*texel, vec2(0.0), vec2(1.0))); }
void main(){
  vec4 s=texture(srcTex,uv); vec3 rgb=s.rgb; float a=clamp(amount,0.0,1.0);
  if(effectKind==0 || effectKind==2 || effectKind==8){
    float st=max(1.0,radius*(quality==2?0.5:0.25)); vec2 axis=vec2(st,0.0);
    if(effectKind==8){ float r=radians(angle); axis=vec2(cos(r),sin(r))*st; }
    else if(passIndex>0) axis=vec2(0.0,st);
    vec3 b=(px(-2.0*axis).rgb+2.0*px(-axis).rgb+4.0*s.rgb+2.0*px(axis).rgb+px(2.0*axis).rgb)/10.0;
    rgb=effectKind==2?mix(s.rgb,s.rgb+max(b-vec3(0.55),vec3(0.0))*2.2222222,a):mix(s.rgb,b,a);
  } else if(effectKind==1){ rgb=mix(s.rgb,s.rgb*5.0-px(vec2(0,-1)).rgb-px(vec2(0,1)).rgb-px(vec2(-1,0)).rgb-px(vec2(1,0)).rgb,a); }
  else if(effectKind==3){ vec2 c=uv*2.0-1.0; float k=1.0+a*0.35*dot(c,c); rgb=texture(srcTex,clamp((c/k+1.0)*0.5,vec2(0),vec2(1))).rgb; }
  else if(effectKind==4 || effectKind==5){ float g=hash21(gl_FragCoord.xy)-0.5; if(effectKind==5){ float l=dot(s.rgb,vec3(.2126,.7152,.0722)); g*=.35+.65*(1.0-abs(l*2.0-1.0)); } rgb=s.rgb+g*a*(effectKind==5?.16:.25); }
  else if(effectKind==6){ float o=max(1.0,1.0+radius*.12); rgb=mix(s.rgb,vec3(px(vec2(o,0)).r,s.g,px(vec2(-o,0)).b),a); }
  else if(effectKind==7){ vec2 c=uv*2.0-1.0; float v=smoothstep(.45,1.15,length(c*vec2(.9,1.0))); rgb=s.rgb*(1.0-v*a*.85); }
  outColor=vec4(max(rgb,vec3(0.0)),s.a);
}
)GLSL";

struct OwnedTexture { GLuint texture{}; GLenum internal_format{}; };

GLuint resolve_texture(const NativeEffectSurface& s) noexcept {
  if (!s.texture_handle) return 0;
  if (!s.engine_owned) return static_cast<GLuint>(s.texture_handle);
  const auto* owned=reinterpret_cast<const OwnedTexture*>(s.texture_handle);
  return owned?owned->texture:0;
}

int effect_kind(const std::string& id) noexcept {
  if(id=="effect.gaussian_blur") return 0; if(id=="effect.sharpen") return 1;
  if(id=="effect.glow") return 2; if(id=="effect.lens_distortion") return 3;
  if(id=="effect.noise") return 4; if(id=="effect.film_grain") return 5;
  if(id=="effect.chromatic_aberration") return 6; if(id=="effect.vignette") return 7;
  if(id=="effect.motion_blur") return 8; return -1;
}

GLuint compile_shader(GLenum type,const char* source,std::string& diagnostic){
  GLuint s=glCreateShader(type); glShaderSource(s,1,&source,nullptr); glCompileShader(s);
  GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(ok) return s;
  GLint n=0; glGetShaderiv(s,GL_INFO_LOG_LENGTH,&n); std::string log(std::max(1,n),'\0');
  glGetShaderInfoLog(s,n,nullptr,log.data()); glDeleteShader(s); diagnostic="GLES shader compile failed: "+log; return 0;
}

struct GlesState final {
  EGLDisplay display{EGL_NO_DISPLAY}; EGLContext context{EGL_NO_CONTEXT};
  std::uint64_t identity{}; bool hdr{}; GLuint program{}, vao{}, fbo{}; std::mutex mutex;
  ~GlesState(){ if(program)glDeleteProgram(program); if(vao)glDeleteVertexArrays(1,&vao); if(fbo)glDeleteFramebuffers(1,&fbo); }
  bool current(std::string& d) const { if(eglGetCurrentDisplay()!=display||eglGetCurrentContext()!=context){d="Android GLES effect context is not current";return false;} return true; }
  bool initialize(std::string& d){
    if(!current(d))return false; GLuint vs=compile_shader(GL_VERTEX_SHADER,kVertexShader,d); if(!vs)return false;
    GLuint fs=compile_shader(GL_FRAGMENT_SHADER,kFragmentShader,d); if(!fs){glDeleteShader(vs);return false;}
    program=glCreateProgram(); glAttachShader(program,vs); glAttachShader(program,fs); glLinkProgram(program); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok=0; glGetProgramiv(program,GL_LINK_STATUS,&ok); if(!ok){d="GLES effect program link failed";return false;}
    glGenVertexArrays(1,&vao); glGenFramebuffers(1,&fbo); return glGetError()==GL_NO_ERROR;
  }
};

GLenum format_for(NativeEffectFormat f,bool hdr) noexcept {
  if(f==NativeEffectFormat::rgba16_float) return hdr?GL_RGBA16F:0;
  if(f==NativeEffectFormat::rgba8_unorm||f==NativeEffectFormat::bgra8_unorm) return GL_RGBA8;
  return 0;
}

} // namespace

AndroidGlesEffectProviderResult create_android_gles_effect_provider(AndroidGlesEffectProviderBindings b) noexcept {
  AndroidGlesEffectProviderResult out{};
  if(!b.egl_display||!b.egl_context||!b.device_identity){out.diagnostic="Android GLES effect bindings are incomplete";return out;}
  if(!b.supports_external_textures||!b.supports_external_synchronization){out.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;out.diagnostic="Android GLES effects require shared external textures and synchronization";return out;}
  auto state=std::make_shared<GlesState>(); state->display=reinterpret_cast<EGLDisplay>(b.egl_display); state->context=reinterpret_cast<EGLContext>(b.egl_context); state->identity=b.device_identity; state->hdr=b.supports_rgba16f;
  std::string diagnostic; if(!state->initialize(diagnostic)){out.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;out.diagnostic=std::move(diagnostic);return out;}
  NativeEffectBackendProvider p{}; p.backend=NativeEffectBackend::opengl_es; p.device_identity=state->identity; p.supports_external_memory=true; p.supports_external_synchronization=true; p.supports_hdr=state->hdr;
  p.pass_count=[](const EffectDescriptor& d,const EffectInstance&,EffectQuality){return d.type==EffectType::blur||d.type==EffectType::glow||d.type==EffectType::motion_blur?2u:1u;};
  p.allocate_transient=[state](const NativeEffectSurface& proto,NativeEffectSurface& output,std::string& d){
    std::lock_guard<std::mutex> lock(state->mutex); if(!state->current(d))return false; GLenum internal=format_for(proto.format,state->hdr); if(!internal){d="unsupported GLES effect format";return false;}
    auto owned=std::make_unique<OwnedTexture>(); owned->internal_format=internal; glGenTextures(1,&owned->texture); glBindTexture(GL_TEXTURE_2D,owned->texture); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE); glTexStorage2D(GL_TEXTURE_2D,1,internal,proto.width,proto.height);
    if(glGetError()!=GL_NO_ERROR){if(owned->texture)glDeleteTextures(1,&owned->texture);d="GLES transient texture allocation failed";return false;}
    output=proto; output.texture_handle=reinterpret_cast<std::uint64_t>(owned.release()); output.device_identity=state->identity; output.engine_owned=true; output.external_memory=false; output.cpu_mappable=false; return true;
  };
  p.release_transient=[](const NativeEffectSurface& s){if(!s.engine_owned||!s.texture_handle)return; std::unique_ptr<OwnedTexture> t(reinterpret_cast<OwnedTexture*>(s.texture_handle)); if(t->texture)glDeleteTextures(1,&t->texture);};
  p.record_pass=[state](const NativeEffectPass& pass,std::string& d){
    std::lock_guard<std::mutex> lock(state->mutex); if(!state->current(d))return false; GLuint in=resolve_texture(pass.input),outtex=resolve_texture(pass.output); if(!in||!outtex||in==outtex){d="invalid GLES effect textures";return false;} int kind=effect_kind(pass.effect.effect_id); if(kind<0){d="unknown GLES effect id: "+pass.effect.effect_id;return false;}
    glBindFramebuffer(GL_FRAMEBUFFER,state->fbo); glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,outtex,0); if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){d="GLES effect framebuffer is incomplete";return false;}
    glViewport(0,0,pass.output.width,pass.output.height); glUseProgram(state->program); glBindVertexArray(state->vao); glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,in);
    glUniform1i(glGetUniformLocation(state->program,"srcTex"),0); glUniform1i(glGetUniformLocation(state->program,"effectKind"),kind); glUniform1i(glGetUniformLocation(state->program,"passIndex"),pass.pass_index); glUniform1i(glGetUniformLocation(state->program,"quality"),static_cast<int>(pass.quality)); glUniform1f(glGetUniformLocation(state->program,"amount"),pass.effect.amount); glUniform1f(glGetUniformLocation(state->program,"radius"),pass.effect.radius); glUniform1f(glGetUniformLocation(state->program,"angle"),pass.effect.angle); glUniform2f(glGetUniformLocation(state->program,"texel"),1.0f/pass.output.width,1.0f/pass.output.height); glUniform2ui(glGetUniformLocation(state->program,"seed"),static_cast<GLuint>(pass.effect.seed),static_cast<GLuint>(pass.effect.seed>>32u)); glDrawArrays(GL_TRIANGLES,0,3);
    if(glGetError()!=GL_NO_ERROR){d="GLES effect draw failed";return false;} return true;
  };
  p.submit=[state](std::string& d){std::lock_guard<std::mutex> lock(state->mutex); if(!state->current(d))return false; glFlush(); glFinish(); if(glGetError()!=GL_NO_ERROR){d="GLES effect completion failed";return false;} return true;};
  if(!validate_native_effect_provider(p,diagnostic)){out.result=DIGITOR_RESULT_BACKEND_UNAVAILABLE;out.diagnostic=std::move(diagnostic);return out;}
  out.provider=std::move(p); out.lifetime=std::move(state); out.result=DIGITOR_RESULT_OK; return out;
}

} // namespace digitor

#else
namespace digitor { AndroidGlesEffectProviderResult create_android_gles_effect_provider(AndroidGlesEffectProviderBindings) noexcept { AndroidGlesEffectProviderResult out{}; out.result=DIGITOR_RESULT_UNSUPPORTED; out.diagnostic="Android GLES effect provider is only available on Android"; return out; } }
#endif
