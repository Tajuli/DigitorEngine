from pathlib import Path

path = Path(__file__).resolve().parents[1] / "src/gpu/vulkan_backend.cpp"
text = path.read_text()

old_include = """#include <android/hardware_buffer.h>\n#include <unistd.h>\n#endif\n#include <vulkan/vulkan.h>\nnamespace digitor {\nnamespace {\nstruct VulkanLiveResources {\n"""
new_include = """#include <android/hardware_buffer.h>\n#include <dlfcn.h>\n#include <unistd.h>\n#endif\n#include <vulkan/vulkan.h>\nnamespace digitor {\nnamespace {\n#if defined(__ANDROID__)\nusing AHardwareBufferDescribeFn =\n    void (*)(const AHardwareBuffer*, AHardwareBuffer_Desc*);\n\nAHardwareBufferDescribeFn resolve_ahardwarebuffer_describe() noexcept {\n  static const auto describe =\n      reinterpret_cast<AHardwareBufferDescribeFn>(\n          dlsym(RTLD_DEFAULT, \"AHardwareBuffer_describe\"));\n  return describe;\n}\n#endif\n\nstruct VulkanLiveResources {\n"""
if text.count(old_include) != 1:
    raise RuntimeError(f"include/helper anchor count={text.count(old_include)}")
text = text.replace(old_include, new_include, 1)

old_call = """    auto* ahb = reinterpret_cast<AHardwareBuffer*>(descriptor.native_handle);\n    AHardwareBuffer_Desc ahb_descriptor{};\n    AHardwareBuffer_describe(ahb, &ahb_descriptor);\n    if (ahb_descriptor.width != descriptor.width ||\n"""
new_call = """    auto* ahb = reinterpret_cast<AHardwareBuffer*>(descriptor.native_handle);\n    const auto describe_ahardwarebuffer = resolve_ahardwarebuffer_describe();\n    if (!describe_ahardwarebuffer)\n      return DIGITOR_RESULT_BACKEND_UNAVAILABLE;\n    AHardwareBuffer_Desc ahb_descriptor{};\n    describe_ahardwarebuffer(ahb, &ahb_descriptor);\n    if (ahb_descriptor.width != descriptor.width ||\n"""
if text.count(old_call) != 1:
    raise RuntimeError(f"describe call anchor count={text.count(old_call)}")
text = text.replace(old_call, new_call, 1)

if "AHardwareBuffer_describe(ahb" in text:
    raise RuntimeError("direct API26 AHardwareBuffer_describe call remains")

path.write_text(text)
print("Android API24-safe AHardwareBuffer runtime resolver applied")
