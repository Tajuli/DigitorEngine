#include "digitor/native_node_platform_factories.hpp"
#if defined(__ANDROID__)
#include <GLES3/gl31.h>
#include <string>
#include <algorithm>
#endif
namespace digitor {
bool create_gles_native_node_pipeline(const NativeNodePlatformFactoryContext&,const NativeNodeCompiledPipeline& compiled,
 const NativeNodeShaderBinary& binary,NativeNodeBackendPipelineHandle& out,std::string& diagnostic) noexcept {
 out={};
#if defined(__ANDROID__)
 if(compiled.backend!=DIGITOR_RENDERER_OPENGL_ES||binary.format!=NativeNodeBinaryFormat::glsl_es||!binary.valid_for(compiled)||binary.bytes.empty()){diagnostic="invalid GLES native-node pipeline input";return false;}
 std::string source(reinterpret_cast<const char*>(binary.bytes.data()),binary.bytes.size()); const char* p=source.c_str();
 GLuint shader=glCreateShader(GL_COMPUTE_SHADER); glShaderSource(shader,1,&p,nullptr); glCompileShader(shader); GLint ok=0; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
 if(!ok){glDeleteShader(shader);diagnostic="GLES compute shader compilation failed";return false;}
 GLuint program=glCreateProgram();glAttachShader(program,shader);glLinkProgram(program);glDeleteShader(shader);glGetProgramiv(program,GL_LINK_STATUS,&ok);
 if(!ok){glDeleteProgram(program);diagnostic="GLES compute program link failed";return false;}
 out.pipeline=program;diagnostic.clear();return true;
#else
 (void)compiled;(void)binary;diagnostic="GLES support not compiled";return false;
#endif
}
void destroy_gles_native_node_pipeline(const NativeNodePlatformFactoryContext&,const NativeNodeBackendPipelineHandle& h) noexcept {
#if defined(__ANDROID__)
 if(h.pipeline) glDeleteProgram(static_cast<GLuint>(h.pipeline));
#else
 (void)h;
#endif
}
bool record_gles_native_node_dispatch(const NativeNodePlatformFactoryContext&,const NativeNodeBackendPipelineHandle& h,
 const NativeNodeDispatchGeometry& geometry,const NativeNodeDispatchResources& resources,std::string& diagnostic) noexcept {
#if defined(__ANDROID__)
 if(!h.pipeline||geometry.groups_x==0||geometry.groups_y==0||geometry.groups_z==0){diagnostic="invalid GLES node dispatch context";return false;}
 GLuint program=static_cast<GLuint>(h.pipeline); glUseProgram(program);
 const auto contract=native_node_pipeline_contract(DIGITOR_RENDERER_OPENGL_ES,resources.kernel);
 if(!validate_native_node_pipeline_contract(contract)){diagnostic="invalid GLES node kernel contract";return false;}
 GLuint constant_binding=0;
 for(std::uint32_t i=0;i<contract.binding_count;++i){
  const auto& expected=contract.bindings[i];
  if(expected.kind==NativeNodeBindingKind::constants){constant_binding=expected.binding;continue;}
  auto it=std::find_if(resources.textures.begin(),resources.textures.end(),[&](const auto&t){return t.slot==expected.binding;});
  if(it==resources.textures.end()||!it->native_texture){diagnostic="invalid GLES texture binding";return false;}
  const GLenum access=expected.kind==NativeNodeBindingKind::storage_output?GL_WRITE_ONLY:GL_READ_ONLY;
  const GLenum format=expected.format=="r32f"?GL_R32F:GL_RGBA32F;
  glBindImageTexture(expected.binding,static_cast<GLuint>(it->native_texture),0,GL_FALSE,0,access,format);
 }
 GLuint ubo=0;glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,resources.constants.size(),resources.constants.data(),GL_STREAM_DRAW);
 glBindBufferBase(GL_UNIFORM_BUFFER,constant_binding,ubo);
 glDispatchCompute(geometry.groups_x,geometry.groups_y,geometry.groups_z);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);glFinish();
 glBindBufferBase(GL_UNIFORM_BUFFER,constant_binding,0);glDeleteBuffers(1,&ubo); GLenum error=glGetError(); if(error!=GL_NO_ERROR){diagnostic="GLES node dispatch failed";return false;} diagnostic.clear();return true;
#else
 (void)h;(void)geometry;(void)resources;diagnostic="GLES support not compiled";return false;
#endif
}
} // namespace digitor
