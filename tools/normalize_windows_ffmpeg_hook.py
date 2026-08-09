from pathlib import Path

path = Path('dart/digitor_engine_ffi/hook/build.dart')
text = path.read_text()

needle = """  if (code.targetOS != OS.windows) return null;
  if (!Platform.isWindows) {
"""
replacement = """  if (code.targetOS != OS.windows) return null;
  if (!Platform.isWindows) {
"""
if needle not in text:
    raise SystemExit('Could not locate Windows FFmpeg hook entry point')

platform_guard_end = """    );
  }

  final triplet = switch (code.targetArchitecture.name) {
"""
auto_provision = """    );
  }

  if (code.targetArchitecture.name == 'x64') {
    final provisioner = File(
      '${Directory.current.path}${Platform.pathSeparator}tool'
      '${Platform.pathSeparator}provision_ffmpeg_windows.ps1',
    );
    if (!await provisioner.exists()) {
      throw StateError(
        'Windows FFmpeg provisioner is missing: ${provisioner.path}',
      );
    }
    await _run(
      'powershell.exe',
      <String>[
        '-NoProfile',
        '-ExecutionPolicy',
        'Bypass',
        '-File',
        provisioner.path,
      ],
      workingDirectory: Directory.current.uri,
    );
  }

  final triplet = switch (code.targetArchitecture.name) {
"""
if auto_provision not in text:
    if platform_guard_end not in text:
        raise SystemExit('Could not locate Windows FFmpeg triplet block')
    text = text.replace(platform_guard_end, auto_provision, 1)

path.write_text(text)
