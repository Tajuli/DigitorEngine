import 'dart:io';

import 'package:code_assets/code_assets.dart';
import 'package:hooks/hooks.dart';

class _FfmpegSdk {
  const _FfmpegSdk(this.root, this.runtimeLibraries);

  final Uri root;
  final List<File> runtimeLibraries;
}

Future<void> main(List<String> arguments) async {
  await build(arguments, (input, output) async {
    if (!input.config.buildCodeAssets) return;

    final code = input.config.code;
    final targetOs = code.targetOS;
    if (!const <OS>{
      OS.android,
      OS.iOS,
      OS.linux,
      OS.macOS,
      OS.windows,
    }.contains(targetOs)) {
      throw UnsupportedError(
        'DigitorEngine does not support ${targetOs.name} native assets.',
      );
    }

    final engineRoot = input.packageRoot.resolve('../../');
    final outputRoot = input.outputDirectory;
    final buildDirectory = outputRoot.resolve('cmake/');
    final libraryUri = outputRoot.resolve(
      targetOs.dylibFileName('digitor_engine'),
    );

    final ffmpegSdk = await _resolveFfmpegSdk(
      code,
      input.userDefines.path('ffmpeg_root'),
      input.packageRoot,
    );
    final ffmpegRoot = ffmpegSdk?.root;
    if (ffmpegRoot != null) {
      output.dependencies.add(ffmpegRoot);
    }

    output.dependencies.addAll(<Uri>[
      engineRoot.resolve('CMakeLists.txt'),
      engineRoot.resolve('cmake/'),
      engineRoot.resolve('include/'),
      engineRoot.resolve('src/'),
    ]);

    await Directory.fromUri(buildDirectory).create(recursive: true);
    await Directory.fromUri(outputRoot).create(recursive: true);

    final cmake = await _findCmake();
    final configureArguments = <String>[
      '-S',
      engineRoot.toFilePath(),
      '-B',
      buildDirectory.toFilePath(),
      '-DDIGITOR_BUILD_SHARED=ON',
      '-DBUILD_SHARED_LIBS=ON',
      '-DDIGITOR_BUILD_TESTS=OFF',
      '-DDIGITOR_BUILD_EXAMPLES=OFF',
      '-DDIGITOR_ENABLE_FFMPEG=ON',
      '-DDIGITOR_REQUIRE_FFMPEG=${ffmpegRoot == null ? 'OFF' : 'ON'}',
      if (ffmpegRoot != null)
        '-DDIGITOR_FFMPEG_ROOT=${ffmpegRoot.toFilePath()}',
      '-DDIGITOR_ENABLE_OCIO=ON',
      '-DDIGITOR_REQUIRE_OCIO=OFF',
      '-DDIGITOR_ENABLE_NATIVE_SHADER_COMPILER=ON',
      '-DDIGITOR_WARNINGS_AS_ERRORS=OFF',
      '-DCMAKE_BUILD_TYPE=Release',
      '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${outputRoot.toFilePath()}',
      '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${outputRoot.toFilePath()}',
      '-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=${outputRoot.toFilePath()}',
      '-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=${outputRoot.toFilePath()}',
      ..._platformCmakeArguments(code),
    ];

    await _run(cmake, configureArguments, workingDirectory: engineRoot);
    await _run(cmake, <String>[
      '--build',
      buildDirectory.toFilePath(),
      '--config',
      'Release',
      '--target',
      'digitor_engine',
      '--parallel',
      '2',
    ], workingDirectory: engineRoot);

    final library = File.fromUri(libraryUri);
    if (!await library.exists()) {
      throw StateError(
        'DigitorEngine build completed but ${library.path} was not produced.',
      );
    }

    output.assets.code.add(
      CodeAsset(
        package: input.packageName,
        name: 'digitor_engine_ffi.dart',
        linkMode: DynamicLoadingBundled(),
        file: libraryUri,
      ),
    );

    if (targetOs == OS.windows && ffmpegSdk != null) {
      for (final source in ffmpegSdk.runtimeLibraries) {
        final fileName = source.uri.pathSegments.last;
        final bundledUri = outputRoot.resolve(fileName);
        final bundledFile = File.fromUri(bundledUri);
        if (source.absolute.path != bundledFile.absolute.path) {
          await source.copy(bundledFile.path);
        }
        output.assets.code.add(
          CodeAsset(
            package: input.packageName,
            name: 'ffmpeg/$fileName',
            linkMode: DynamicLoadingBundled(),
            file: bundledUri,
          ),
        );
      }
    }
  });
}

Future<_FfmpegSdk?> _resolveFfmpegSdk(
  CodeConfig code,
  Uri? configuredRoot,
  Uri packageRoot,
) async {
  if (configuredRoot != null) {
    final directory = Directory.fromUri(configuredRoot);
    if (!await directory.exists()) {
      throw ArgumentError(
        'hooks.user_defines.digitor_engine_ffi.ffmpeg_root does not exist: '
        '${directory.path}',
      );
    }
    return _FfmpegSdk(
      configuredRoot,
      code.targetOS == OS.windows
          ? await _windowsRuntimeLibraries(directory)
          : const <File>[],
    );
  }

  if (code.targetOS != OS.windows) return null;
  if (!Platform.isWindows) {
    throw UnsupportedError(
      'Automatic Windows FFmpeg provisioning requires a Windows build host. '
      'Set hooks.user_defines.digitor_engine_ffi.ffmpeg_root when cross-building.',
    );
  }

  if (code.targetArchitecture.name == 'x64') {
    final provisioner = File.fromUri(
      packageRoot.resolve('tool/provision_ffmpeg_windows.ps1'),
    );
    if (!await provisioner.exists()) {
      throw StateError(
        'Windows FFmpeg provisioner is missing from the DigitorEngine package: '
        '${provisioner.path}',
      );
    }
    await _run('powershell.exe', <String>[
      '-NoProfile',
      '-ExecutionPolicy',
      'Bypass',
      '-File',
      provisioner.path,
    ], workingDirectory: packageRoot);
  }

  final triplet = switch (code.targetArchitecture.name) {
    'arm64' => 'arm64-windows',
    'ia32' => 'x86-windows',
    'x64' => 'x64-windows',
    _ => throw UnsupportedError(
      'Unsupported Windows FFmpeg architecture: '
      '${code.targetArchitecture.name}',
    ),
  };

  final profile = _windowsDependencyProfile();
  final cacheRoot = Directory(
    '${profile.localAppData}${Platform.pathSeparator}DigitorEngine'
    '${Platform.pathSeparator}deps',
  );
  await cacheRoot.create(recursive: true);

  final vcpkgRoot = Directory(
    '${cacheRoot.path}${Platform.pathSeparator}vcpkg',
  );
  final vcpkg = File('${vcpkgRoot.path}${Platform.pathSeparator}vcpkg.exe');

  if (!await vcpkg.exists()) {
    if (await vcpkgRoot.exists()) {
      await vcpkgRoot.delete(recursive: true);
    }
    await _run('git', <String>[
      'clone',
      '--depth',
      '1',
      'https://github.com/microsoft/vcpkg.git',
      vcpkgRoot.path,
    ], workingDirectory: cacheRoot.uri);
    await _run(
      '${vcpkgRoot.path}${Platform.pathSeparator}bootstrap-vcpkg.bat',
      const <String>['-disableMetrics'],
      workingDirectory: vcpkgRoot.uri,
      environmentOverrides: <String, String>{'VCPKG_ROOT': vcpkgRoot.path},
    );
  }

  final sdkRoot = Directory(
    '${vcpkgRoot.path}${Platform.pathSeparator}installed'
    '${Platform.pathSeparator}$triplet',
  );
  final avcodecHeader = File(
    '${sdkRoot.path}${Platform.pathSeparator}include'
    '${Platform.pathSeparator}libavcodec${Platform.pathSeparator}avcodec.h',
  );
  final avformatLib = File(
    '${sdkRoot.path}${Platform.pathSeparator}lib'
    '${Platform.pathSeparator}avformat.lib',
  );

  if (!await avcodecHeader.exists() || !await avformatLib.exists()) {
    await _installWindowsFfmpeg(
      code: code,
      vcpkg: vcpkg,
      vcpkgRoot: vcpkgRoot,
      triplet: triplet,
      cacheRoot: cacheRoot,
    );
  }

  if (!await avcodecHeader.exists() || !await avformatLib.exists()) {
    throw StateError(
      'FFmpeg provisioning completed without the required development SDK at '
      '${sdkRoot.path}.',
    );
  }

  final runtimeLibraries = await _windowsRuntimeLibraries(sdkRoot);
  if (runtimeLibraries.isEmpty) {
    throw StateError('FFmpeg SDK at ${sdkRoot.path} contains no runtime DLLs.');
  }
  return _FfmpegSdk(sdkRoot.uri, runtimeLibraries);
}

Future<void> _installWindowsFfmpeg({
  required CodeConfig code,
  required File vcpkg,
  required Directory vcpkgRoot,
  required String triplet,
  required Directory cacheRoot,
}) async {
  final compiler = code.cCompiler?.compiler;
  if (compiler == null) {
    throw StateError(
      'Flutter did not provide the Windows C compiler needed to locate the '
      'Visual Studio developer environment.',
    );
  }

  final vcvars = await _windowsVcvars(compiler, code.targetArchitecture.name);
  final visualStudioRoot = _visualStudioRootFromVcvars(vcvars);
  final script = File(
    '${cacheRoot.path}${Platform.pathSeparator}install-ffmpeg-$triplet.cmd',
  );
  await script.writeAsString(
    '@echo off\r\n'
    'call "${vcvars.path}" >nul\r\n'
    'if errorlevel 1 exit /b %errorlevel%\r\n'
    'set "VCPKG_ROOT=${vcpkgRoot.path}"\r\n'
    'set "VCPKG_VISUAL_STUDIO_PATH=${visualStudioRoot.path}"\r\n'
    'set "VCPKG_DISABLE_METRICS=1"\r\n'
    '"${vcpkg.path}" install '
    'ffmpeg[avcodec,avformat,swresample,swscale]:$triplet '
    '--clean-after-build --disable-metrics '
    '--vcpkg-root "${vcpkgRoot.path}"\r\n'
    'exit /b %errorlevel%\r\n',
  );

  try {
    await _run(
      'cmd.exe',
      <String>['/d', '/c', script.path],
      workingDirectory: vcpkgRoot.uri,
      environmentOverrides: <String, String>{
        'VCPKG_ROOT': vcpkgRoot.path,
        'VCPKG_VISUAL_STUDIO_PATH': visualStudioRoot.path,
        'VCPKG_DISABLE_METRICS': '1',
      },
    );
  } finally {
    if (await script.exists()) {
      await script.delete();
    }
  }
}

Directory _visualStudioRootFromVcvars(File vcvars) {
  var directory = vcvars.parent;
  for (var i = 0; i < 3; i++) {
    directory = directory.parent;
  }
  return directory;
}

Future<File> _windowsVcvars(Uri compiler, String architecture) async {
  final scriptName = switch (architecture) {
    'arm64' => 'vcvarsamd64_arm64.bat',
    'ia32' => 'vcvars32.bat',
    'x64' => 'vcvars64.bat',
    _ => throw UnsupportedError(
      'Unsupported Windows developer environment architecture: $architecture',
    ),
  };

  var directory = File.fromUri(compiler).parent;
  for (var depth = 0; depth < 12; depth++) {
    final candidate = File(
      '${directory.path}${Platform.pathSeparator}Auxiliary'
      '${Platform.pathSeparator}Build${Platform.pathSeparator}$scriptName',
    );
    if (await candidate.exists()) {
      return candidate;
    }
    final parent = directory.parent;
    if (parent.path == directory.path) break;
    directory = parent;
  }

  throw StateError(
    'Unable to locate $scriptName from Windows compiler '
    '${compiler.toFilePath()}.',
  );
}

class _WindowsDependencyProfile {
  const _WindowsDependencyProfile(
    this.userProfile,
    this.localAppData,
    this.appData,
    this.temp,
  );

  final String userProfile;
  final String localAppData;
  final String appData;
  final String temp;

  Map<String, String> get environment => <String, String>{
    'USERPROFILE': userProfile,
    'LOCALAPPDATA': localAppData,
    'APPDATA': appData,
    'TEMP': temp,
    'TMP': temp,
  };
}

_WindowsDependencyProfile _windowsDependencyProfile() {
  final environment = Platform.environment;
  final temp =
      environment['TEMP'] ?? environment['TMP'] ?? Directory.systemTemp.path;
  var userProfile = environment['USERPROFILE'];
  if (userProfile == null || userProfile.isEmpty) {
    final homeDrive = environment['HOMEDRIVE'];
    final homePath = environment['HOMEPATH'];
    if (homeDrive != null &&
        homePath != null &&
        homeDrive.isNotEmpty &&
        homePath.isNotEmpty) {
      userProfile = '$homeDrive$homePath';
    }
  }
  if (userProfile == null || userProfile.isEmpty) {
    userProfile =
        '${Directory.systemTemp.path}${Platform.pathSeparator}'
        'DigitorEngine-profile';
  }

  final localAppData = (environment['LOCALAPPDATA']?.isNotEmpty ?? false)
      ? environment['LOCALAPPDATA']!
      : '$userProfile${Platform.pathSeparator}AppData'
            '${Platform.pathSeparator}Local';
  final appData = (environment['APPDATA']?.isNotEmpty ?? false)
      ? environment['APPDATA']!
      : '$userProfile${Platform.pathSeparator}AppData'
            '${Platform.pathSeparator}Roaming';

  Directory(userProfile).createSync(recursive: true);
  Directory(localAppData).createSync(recursive: true);
  Directory(appData).createSync(recursive: true);
  Directory(temp).createSync(recursive: true);
  return _WindowsDependencyProfile(userProfile, localAppData, appData, temp);
}

Future<List<File>> _windowsRuntimeLibraries(Directory sdkRoot) async {
  final bin = Directory('${sdkRoot.path}${Platform.pathSeparator}bin');
  if (!await bin.exists()) return const <File>[];
  final files = <File>[];
  await for (final entity in bin.list(followLinks: false)) {
    if (entity is File && entity.path.toLowerCase().endsWith('.dll')) {
      files.add(entity);
    }
  }
  files.sort((a, b) => a.path.compareTo(b.path));
  return files;
}

List<String> _platformCmakeArguments(CodeConfig code) {
  final architecture = code.targetArchitecture.name;
  switch (code.targetOS) {
    case OS.android:
      final compiler = code.cCompiler?.compiler;
      if (compiler == null) {
        throw StateError('Flutter did not provide an Android C toolchain.');
      }
      final ndk = _androidNdkRoot(compiler);
      return <String>[
        '-DCMAKE_TOOLCHAIN_FILE=${ndk.resolve('build/cmake/android.toolchain.cmake').toFilePath()}',
        '-DANDROID_NDK=${ndk.toFilePath()}',
        '-DANDROID_ABI=${_androidAbi(architecture)}',
        '-DANDROID_PLATFORM=android-${code.android.targetNdkApi}',
        '-DANDROID_STL=c++_static',
      ];
    case OS.iOS:
      final simulator = code.iOS.targetSdk == IOSSdk.iPhoneSimulator;
      return <String>[
        '-DCMAKE_SYSTEM_NAME=iOS',
        '-DCMAKE_OSX_SYSROOT=${simulator ? 'iphonesimulator' : 'iphoneos'}',
        '-DCMAKE_OSX_ARCHITECTURES=${_appleArchitecture(architecture)}',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=${code.iOS.targetVersion}',
      ];
    case OS.macOS:
      return <String>[
        '-DCMAKE_OSX_ARCHITECTURES=${_appleArchitecture(architecture)}',
        '-DCMAKE_OSX_DEPLOYMENT_TARGET=${code.macOS.targetVersion}',
      ];
    case OS.windows:
      if (architecture == 'arm64') {
        return const <String>['-A', 'ARM64'];
      }
      if (architecture == 'ia32') {
        return const <String>['-A', 'Win32'];
      }
      return const <String>[];
    case OS.linux:
      return const <String>[];
    default:
      throw UnsupportedError('Unsupported target ${code.targetOS.name}.');
  }
}

Uri _androidNdkRoot(Uri compiler) {
  var directory = File.fromUri(compiler).parent;
  for (var i = 0; i < 5; i++) {
    directory = directory.parent;
  }
  final toolchain = File(
    '${directory.path}${Platform.pathSeparator}build'
    '${Platform.pathSeparator}cmake${Platform.pathSeparator}'
    'android.toolchain.cmake',
  );
  if (!toolchain.existsSync()) {
    throw StateError(
      'Unable to derive Android NDK root from compiler ${compiler.toFilePath()}.',
    );
  }
  return directory.uri;
}

String _androidAbi(String architecture) => switch (architecture) {
  'arm' => 'armeabi-v7a',
  'arm64' => 'arm64-v8a',
  'ia32' => 'x86',
  'x64' => 'x86_64',
  _ => throw UnsupportedError(
    'Unsupported Android architecture: $architecture.',
  ),
};

String _appleArchitecture(String architecture) => switch (architecture) {
  'arm64' => 'arm64',
  'x64' => 'x86_64',
  _ => throw UnsupportedError('Unsupported Apple architecture: $architecture'),
};

Future<String> _findCmake() async {
  final override = Platform.environment['CMAKE'];
  if (override != null && override.isNotEmpty && File(override).existsSync()) {
    return override;
  }
  try {
    final probe = await Process.run('cmake', const <String>['--version']);
    if (probe.exitCode == 0) return 'cmake';
  } on ProcessException {
    // Fall through to well-known SDK/install locations.
  }

  final executable = Platform.isWindows ? 'cmake.exe' : 'cmake';
  final candidates = <String>[
    if (Platform.isWindows) r'C:\Program Files\CMake\bin\cmake.exe',
    if (Platform.isMacOS) '/opt/homebrew/bin/cmake',
    if (Platform.isMacOS) '/usr/local/bin/cmake',
  ];
  final sdkRoot =
      Platform.environment['ANDROID_SDK_ROOT'] ??
      Platform.environment['ANDROID_HOME'];
  if (sdkRoot != null && sdkRoot.isNotEmpty) {
    final cmakeRoot = Directory('$sdkRoot${Platform.pathSeparator}cmake');
    if (cmakeRoot.existsSync()) {
      final installs = cmakeRoot.listSync().whereType<Directory>().toList()
        ..sort((a, b) => b.path.compareTo(a.path));
      for (final install in installs) {
        candidates.add(
          '${install.path}${Platform.pathSeparator}bin'
          '${Platform.pathSeparator}$executable',
        );
      }
    }
  }

  for (final candidate in candidates) {
    if (File(candidate).existsSync()) {
      return candidate;
    }
  }

  throw StateError(
    'CMake 3.21+ is required to build DigitorEngine. '
    'Install CMake or set the CMAKE environment variable.',
  );
}

Future<void> _run(
  String executable,
  List<String> arguments, {
  required Uri workingDirectory,
  Map<String, String>? environmentOverrides,
}) async {
  final dependencyEnvironment = Platform.isWindows
      ? <String, String>{
          ..._windowsDependencyProfile().environment,
          ...?environmentOverrides,
        }
      : environmentOverrides;
  final result = await Process.run(
    executable,
    arguments,
    workingDirectory: workingDirectory.toFilePath(),
    runInShell: Platform.isWindows,
    environment: dependencyEnvironment,
    includeParentEnvironment: true,
  );
  if (result.stdout.toString().isNotEmpty) {
    stdout.write(result.stdout);
  }
  if (result.stderr.toString().isNotEmpty) {
    stderr.write(result.stderr);
  }
  if (result.exitCode != 0) {
    throw ProcessException(
      executable,
      arguments,
      '${result.stdout}\n${result.stderr}',
      result.exitCode,
    );
  }
}
