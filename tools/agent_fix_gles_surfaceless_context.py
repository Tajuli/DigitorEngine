from pathlib import Path
p=Path('src/gpu/gles_backend.cpp')
s=p.read_text()
old='''  bool make_context_current() noexcept {\n    return display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT &&\n           surface_ != EGL_NO_SURFACE &&\n           eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;\n  }\n'''
new='''  bool make_context_current() noexcept {\n    return display_ != EGL_NO_DISPLAY && context_ != EGL_NO_CONTEXT &&\n           eglMakeCurrent(display_, surface_, surface_, context_) == EGL_TRUE;\n  }\n'''
assert old in s
p.write_text(s.replace(old,new,1))
