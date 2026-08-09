from pathlib import Path

path = Path('dart/digitor_engine_ffi/hook/build.dart')
text = path.read_text()
old = """        'clone',
        '--depth',
        '1',
        'https://github.com/microsoft/vcpkg.git',
        vcpkgRoot.path,
"""
new = """        'clone',
        '--depth',
        '1',
        '--branch',
        '2025.06.13',
        'https://github.com/microsoft/vcpkg.git',
        vcpkgRoot.path,
"""
if old in text:
    text = text.replace(old, new, 1)
text = text.replace(
    "'${cacheRoot.path}${Platform.pathSeparator}vcpkg',",
    "'${cacheRoot.path}${Platform.pathSeparator}vcpkg-2025.06.13',",
    1,
)
path.write_text(text)
