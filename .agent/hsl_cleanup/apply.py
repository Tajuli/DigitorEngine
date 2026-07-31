from pathlib import Path
import re


def once(text, old, new, label):
    n=text.count(old)
    if n!=1: raise SystemExit(f'{label}: expected 1 anchor, found {n}')
    return text.replace(old,new,1)

p=Path('src/gpu/native_hsl_qualifier.cpp');s=p.read_text()
start=s.index('  // The current native shader is a single per-pixel pass.')
end=s.index('  NativeHslQualifierParameters n;',start)
s=s[:start]+s[end:]
s=s.replace('digitor-hsl-qualifier-v5.0.0-schema1','digitor-hsl-qualifier-v5.1.0-cleanup-schema1')
p.write_text(s)

p=Path('tests/test_hsl_qualifier.cpp');s=p.read_text()
old='''  auto cleanup=settings;cleanup.blur=1.0f;\n  const auto blurred=HslQualifierParameters::create(cleanup);bad=false;\n  try{(void)native_hsl_qualifier_parameters(*blurred,7,5);}\n  catch(const std::invalid_argument&){bad=true;}\n  assert(bad);\n  cleanup.blur=0.0f;cleanup.denoise=0.5f;\n  const auto denoised=HslQualifierParameters::create(cleanup);bad=false;\n  try{(void)native_hsl_qualifier_parameters(*denoised,7,5);}\n  catch(const std::invalid_argument&){bad=true;}\n  assert(bad);\n'''
new='''  auto cleanup=settings;cleanup.blur=1.0f;\n  const auto blurred=HslQualifierParameters::create(cleanup);\n  const auto native_blurred=native_hsl_qualifier_parameters(*blurred,7,5);\n  assert(native_blurred.cleanup.w==1.0f && native_blurred.cleanup.z==0.0f);\n  cleanup.blur=0.0f;cleanup.denoise=0.5f;\n  const auto denoised=HslQualifierParameters::create(cleanup);\n  const auto native_denoised=native_hsl_qualifier_parameters(*denoised,7,5);\n  assert(native_denoised.cleanup.z==0.5f && native_denoised.cleanup.w==0.0f);\n'''
s=once(s,old,new,'unit expectations');p.write_text(s)

specs=[
('src/gpu/d3d12_backend.cpp','  float clean_black, clean_white;\n  std::uint32_t invert, width, height, padding[3];','  float clean_black, clean_white, denoise, blur;\n  std::uint32_t invert, width, height, padding;'),
('src/gpu/vulkan_backend.cpp','  float clean_black, clean_white;\n  std::uint32_t invert, width, height, padding[3];','  float clean_black, clean_white, denoise, blur;\n  std::uint32_t invert, width, height, padding;'),
('src/gpu/metal_backend.mm','    float clean_black, clean_white;\n    std::uint32_t invert, width, height, padding[3];','    float clean_black, clean_white, denoise, blur;\n    std::uint32_t invert, width, height, padding;'),
('src/gpu/gles_backend.cpp','  struct GlHslConstants { float hue[4],saturation[4],luminance[4],clean_black,clean_white; std::uint32_t invert,width,height,padding[3]; };','  struct GlHslConstants { float hue[4],saturation[4],luminance[4],clean_black,clean_white,denoise,blur; std::uint32_t invert,width,height,padding; };')]
for path,old,new in specs:
 p=Path(path);s=p.read_text();p.write_text(once(s,old,new,path+' layout'))

specs=[
('src/gpu/d3d12_backend.cpp','    constants.clean_white = values.clean_white;\n    constants.invert = values.invert ? 1u : 0u;','    constants.clean_white = values.clean_white;\n    constants.denoise = values.denoise;\n    constants.blur = values.blur;\n    constants.invert = values.invert ? 1u : 0u;'),
('src/gpu/vulkan_backend.cpp','    constants.clean_white = values.clean_white;\n    constants.invert = values.invert ? 1u : 0u;','    constants.clean_white = values.clean_white;\n    constants.denoise = values.denoise;\n    constants.blur = values.blur;\n    constants.invert = values.invert ? 1u : 0u;'),
('src/gpu/metal_backend.mm','    c.clean_black = values.clean_black; c.clean_white = values.clean_white;\n    c.invert = values.invert ? 1u : 0u;','    c.clean_black = values.clean_black; c.clean_white = values.clean_white;\n    c.denoise = values.denoise; c.blur = values.blur;\n    c.invert = values.invert ? 1u : 0u;'),
('src/gpu/gles_backend.cpp','c.clean_black=v.clean_black;c.clean_white=v.clean_white;c.invert=v.invert?1u:0u;','c.clean_black=v.clean_black;c.clean_white=v.clean_white;c.denoise=v.denoise;c.blur=v.blur;c.invert=v.invert?1u:0u;')]
for path,old,new in specs:
 p=Path(path);s=p.read_text();p.write_text(once(s,old,new,path+' marshal'))

p=Path('src/gpu/native_node_shader_contracts.cpp');s=p.read_text()
for name in ('kHslHlsl','kHslVk','kHslGles','kHslMsl'):
 src=Path(f'.agent/hsl_cleanup/{name}.txt').read_text()
 pat=rf'constexpr std::string_view {name} = R"\((.*?)\)";'
 s,n=re.subn(pat,f'constexpr std::string_view {name} = R"({src})";',s,count=1,flags=re.S)
 if n!=1: raise SystemExit(f'{name}: shader anchor missing')
p.write_text(s)
