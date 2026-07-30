#include "digitor/native_node_platform_factories.hpp"
#if defined(__ANDROID__)
#include <GLES3/gl31.h>
#include <string>
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
 for(const auto&t:resources.textures){ if(!t.native_texture){diagnostic="invalid GLES texture";return false;} GLenum access=(t.slot==2&&resources.textures.size()==3)||(t.slot==3&&resources.textures.size()==4)?GL_WRITE_ONLY:GL_READ_ONLY; GLenum format=(resources.textures.size()==4&&t.slot==2)?GL_R32F:GL_RGBA32F; glBindImageTexture(t.slot,static_cast<GLuint>(t.native_texture),0,GL_FALSE,0,access,format); }
 GLuint ubo=0;glGenBuffers(1,&ubo);glBindBuffer(GL_UNIFORM_BUFFER,ubo);glBufferData(GL_UNIFORM_BUFFER,resources.constants.size(),resources.constants.data(),GL_STREAM_DRAW);
 const GLuint binding=resources.textures.size()==3?3u:4u;glBindBufferBase(GL_UNIFORM_BUFFER,binding,ubo);
 glDispatchCompute(geometry.groups_x,geometry.groups_y,geometry.groups_z);glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT);glFinish();
 glBindBufferBase(GL_UNIFORM_BUFFER,binding,0);glDeleteBuffers(1,&ubo); GLenum error=glGetError(); if(error!=GL_NO_ERROR){diagnostic="GLES node dispatch failed";return false;} diagnostic.clear();return true;
#else
 (void)h;(void)geometry;(void)resources;diagnostic="GLES support not compiled";return false;
#endif
}
} // namespace digitor
