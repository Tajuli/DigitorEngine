from pathlib import Path
import re


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def replace_raw_const(text: str, name: str, next_name: str, body: str) -> str:
    pattern = re.compile(
        rf'constexpr std::string_view {re.escape(name)} = R"\(.*?\)";\n\s*(?=constexpr std::string_view {re.escape(next_name)})',
        re.S,
    )
    replacement = f'constexpr std::string_view {name} = R"({body})";\n'
    text, count = pattern.subn(lambda _: replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"{name}: expected one raw-string definition")
    return text


gles_path = Path("src/gpu/gles_backend.cpp")
gles = gles_path.read_text(encoding="utf-8")
start_marker = "  DigitorResult dispatch_node_compute(NativeNodeKernel kernel,"
end_marker = "\n\n  DigitorResult import_android_ahardwarebuffer("
start = gles.find(start_marker)
end = gles.find(end_marker, start)
if start < 0 or end < 0:
    raise RuntimeError("GLES native-node dispatch function boundaries not found")

new_dispatch = r'''  DigitorResult dispatch_node_compute(NativeNodeKernel kernel,
      std::uint32_t width, std::uint32_t height,
      std::span<const GLuint> textures, const void* constants,
      std::size_t constant_bytes) noexcept {
    const auto contract=native_node_pipeline_contract(DIGITOR_RENDERER_OPENGL_ES,kernel);
    if(!validate_native_node_pipeline_contract(contract)||!constants||constant_bytes!=contract.constant_bytes)
      return DIGITOR_RESULT_INVALID_ARGUMENT;
    auto pipeline=node_program(kernel); if(!pipeline)return DIGITOR_RESULT_BACKEND_UNAVAILABLE;

    GLint previous_active_texture=GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE,&previous_active_texture);
    GLuint texture_index=0,ubo=0;
    glUseProgram(pipeline->program);
    auto cleanup=[&]() noexcept {
      if(ubo){glBindBuffer(GL_UNIFORM_BUFFER,0);glDeleteBuffers(1,&ubo);}
      glActiveTexture(static_cast<GLenum>(previous_active_texture));
    };

    for(std::uint32_t i=0;i<contract.binding_count;++i){
      const auto& b=contract.bindings[i];
      if(b.kind==NativeNodeBindingKind::constants){
        glGenBuffers(1,&ubo);
        if(!ubo){cleanup();return DIGITOR_RESULT_BACKEND_UNAVAILABLE;}
        glBindBuffer(GL_UNIFORM_BUFFER,ubo);
        glBufferData(GL_UNIFORM_BUFFER,constant_bytes,constants,GL_STREAM_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER,b.binding,ubo);
        continue;
      }
      if(texture_index>=textures.size()){cleanup();return DIGITOR_RESULT_INVALID_ARGUMENT;}
      const GLuint texture=textures[texture_index++];
      if(b.kind==NativeNodeBindingKind::sampled_or_storage_input){
        // GLES image load/store requires the bind format to match the physical
        // texture format. Production decode imports RGBA16F, while later color
        // passes are RGBA32F. Sampling removes that unnecessary format lock and
        // lets one native node kernel consume either float texture without a
        // copy or CPU conversion.
        glActiveTexture(GL_TEXTURE0+b.binding);
        glBindTexture(GL_TEXTURE_2D,texture);
        continue;
      }
      if(b.kind!=NativeNodeBindingKind::storage_output){
        cleanup();return DIGITOR_RESULT_INVALID_ARGUMENT;
      }
      GLenum format=0;
      if(b.format=="r32f") format=GL_R32F;
      else if(b.format=="rgba32f") format=GL_RGBA32F;
      else {cleanup();return DIGITOR_RESULT_INVALID_ARGUMENT;}
      glBindImageTexture(b.binding,texture,0,GL_FALSE,0,GL_WRITE_ONLY,format);
    }
    if(texture_index!=textures.size()){cleanup();return DIGITOR_RESULT_INVALID_ARGUMENT;}

    glDispatchCompute((width+7u)/8u,(height+7u)/8u,1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT|GL_TEXTURE_FETCH_BARRIER_BIT|GL_FRAMEBUFFER_BARRIER_BIT);
    glFinish();
    const GLenum error=glGetError();
    cleanup();
    return error==GL_NO_ERROR?DIGITOR_RESULT_OK:DIGITOR_RESULT_BACKEND_UNAVAILABLE;
  }'''

gles = gles[:start] + new_dispatch + gles[end:]
metadata_old = "GpuFrameMetadata{s.width,s.height,s.format,GpuFrameAlpha::straight,timestamp,s.color_metadata_identity}"
metadata_new = "GpuFrameMetadata{s.width,s.height,DIGITOR_PIXEL_FORMAT_RGBA32_FLOAT,GpuFrameAlpha::straight,timestamp,s.color_metadata_identity}"
metadata_count = gles.count(metadata_old)
if metadata_count != 3:
    raise RuntimeError(f"expected 3 GLES GPU-source metadata copies, found {metadata_count}")
gles = gles.replace(metadata_old, metadata_new)
gles_path.write_text(gles, encoding="utf-8")

contracts_path = Path("src/gpu/native_node_shader_contracts.cpp")
contracts = contracts_path.read_text(encoding="utf-8")
contracts = replace_raw_const(contracts, "kMixerGles", "kCompositeGles", r'''#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2,rgba32f)uniform writeonly highp image2D o;layout(std140,binding=3)uniform P{float wa;float wb;uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;float x=max(p.wa,0.0),y=max(p.wb,0.0),s=max(x+y,1e-20);ivec2 q=ivec2(id);imageStore(o,q,(texelFetch(a,q,0)*x+texelFetch(b,q,0)*y)/s);}''')
contracts = replace_raw_const(contracts, "kCompositeGles", "kHslGles", r'''#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2)uniform highp sampler2D m;layout(binding=3,rgba32f)uniform writeonly highp image2D o;layout(std140,binding=4)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);float t=clamp(texelFetch(m,q,0).r,0.0,1.0);imageStore(o,q,mix(texelFetch(a,q,0),texelFetch(b,q,0),t));}''')
contracts = replace_raw_const(contracts, "kHslGles", "kWindowGles", r'''#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D src;layout(binding=1,r32f)uniform writeonly highp image2D outm;layout(std140,binding=2)uniform P{vec4 hr;vec4 sr;vec4 lr;float cb;float cw;float denoise;float blur;uint inv;uint width;uint height;uint pad;}p;
float lw(float v,vec4 r){if(v>=r.x&&v<=r.y)return 1.0;if(r.z>0.0&&v<r.x&&v>r.x-r.z)return(v-r.x+r.z)/r.z;if(r.z>0.0&&v>r.y&&v<r.y+r.z)return(r.y+r.z-v)/r.z;return 0.0;}float hw(float h,vec4 r){if(r.x<=r.y)return lw(h,r);vec4 a=r;a.y=1.0;vec4 b=r;b.x=0.0;return max(lw(h,a),lw(h,b));}
vec3 hsl(vec3 c){float hi=max(c.r,max(c.g,c.b)),lo=min(c.r,min(c.g,c.b)),d=hi-lo,L=(hi+lo)*.5,S=d==0.0?0.0:d/max(1e-8,1.0-abs(2.0*L-1.0)),H=0.0;if(d!=0.0){if(hi==c.r)H=mod((c.g-c.b)/d,6.0);else if(hi==c.g)H=(c.b-c.r)/d+2.0;else H=(c.r-c.g)/d+4.0;H/=6.0;if(H<0.0)H+=1.0;}return vec3(H,S,L);}float base_at(ivec2 q){q=clamp(q,ivec2(0),ivec2(int(p.width)-1,int(p.height)-1));vec3 v=hsl(texelFetch(src,q,0).rgb);float m=hw(v.x,p.hr)*lw(v.y,p.sr)*lw(v.z,p.lr);if(m<=p.cb)m=0.0;if(m>=1.0-p.cw)m=1.0;if(p.inv!=0u)m=1.0-m;return clamp(m,0.0,1.0);}float dn(ivec2 q){float v[9];int k=0;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++)v[k++]=base_at(q+ivec2(x,y));for(int i=1;i<9;i++){float a=v[i];int j=i-1;while(j>=0&&v[j]>a){v[j+1]=v[j];j--; }v[j+1]=a;}float b=base_at(q);return b+clamp(p.denoise,0.0,1.0)*(v[4]-b);}void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);int r=int(ceil(max(p.blur,0.0)));float sum=0.0;int count=0;if(r==0){sum=dn(q);count=1;}else for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++){sum+=dn(q+ivec2(x,y));count++;}imageStore(outm,q,vec4(clamp(sum/float(count),0.0,1.0)));}''')
contracts = replace_raw_const(contracts, "kMultiplyGles", "kMixerMsl", r'''#version 310 es
precision highp float;precision highp int;layout(local_size_x=8,local_size_y=8)in;layout(binding=0)uniform highp sampler2D a;layout(binding=1)uniform highp sampler2D b;layout(binding=2,r32f)uniform writeonly highp image2D o;layout(std140,binding=3)uniform P{uint width;uint height;}p;void main(){uvec2 id=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(id,uvec2(p.width,p.height))))return;ivec2 q=ivec2(id);imageStore(o,q,vec4(clamp(texelFetch(a,q,0).r,0.0,1.0)*clamp(texelFetch(b,q,0).r,0.0,1.0)));}''')
contracts_path.write_text(contracts, encoding="utf-8")

test_path = Path("tests/test_node_system.cpp")
test = test_path.read_text(encoding="utf-8")
test = replace_once(
    test,
    "      assert(validate_native_node_pipeline_contract(contract));\n      NativeNodeDispatchResources resources;",
    "      assert(validate_native_node_pipeline_contract(contract));\n"
    "      bool has_sampled_input=false;\n"
    "      for(std::uint32_t i=0;i<contract.binding_count;++i)\n"
    "        has_sampled_input=has_sampled_input||contract.bindings[i].kind==NativeNodeBindingKind::sampled_or_storage_input;\n"
    "      if(backend==DIGITOR_RENDERER_OPENGL_ES&&has_sampled_input){\n"
    "        assert(contract.source.find(\"sampler2D\")!=std::string_view::npos);\n"
    "        assert(contract.source.find(\"texelFetch\")!=std::string_view::npos);\n"
    "        assert(contract.source.find(\"uniform readonly highp image2D\")==std::string_view::npos);\n"
    "      }\n"
    "      NativeNodeDispatchResources resources;",
    "GLES sampler-input contract assertions",
)
test_path.write_text(test, encoding="utf-8")

workflow_path = Path(".github/workflows/android-gles-effect-provider.yml")
workflow = workflow_path.read_text(encoding="utf-8")
workflow = replace_once(
    workflow,
    "      - 'src/platform/android/android_gles_effect_provider.cpp'\n",
    "      - 'src/platform/android/android_gles_effect_provider.cpp'\n"
    "      - 'src/gpu/gles_backend.cpp'\n"
    "      - 'src/gpu/native_node_shader_contracts.cpp'\n"
    "      - 'tests/test_node_system.cpp'\n",
    "Android GLES qualification path triggers",
)
workflow_path.write_text(workflow, encoding="utf-8")
