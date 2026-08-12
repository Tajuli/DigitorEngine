from pathlib import Path

path = Path(__file__).resolve().parents[1] / "src/gpu/vulkan_backend.cpp"
text = path.read_text()
old = """    bind_frame_context_lifetime(output);\n    return DIGITOR_RESULT_OK;\n#endif\n  }\n\npublic:\n"""
new = """    bind_frame_context_lifetime(output);\n    return DIGITOR_RESULT_OK;\n#else\n    (void)request;\n    return DIGITOR_RESULT_UNSUPPORTED;\n#endif\n  }\n\npublic:\n"""
if text.count(old) != 1:
    raise RuntimeError(f"expected one import tail anchor, found {text.count(old)}")
path.write_text(text.replace(old, new, 1))
print("restored non-Android/non-Windows Vulkan native-media fail-closed fallback")
