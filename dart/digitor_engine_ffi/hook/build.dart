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
    Uri? androidNinja;
    if (targetOs == OS.android) {
      androidNinja = await _findAndroidNinja(code);
    }
    final configureArguments = <String>[
      if (androidNinja != null) ...<String>[
        '-G',
        'Ninja',
        '-DCMAKE_MAKE_PROGRAM=${androidNinja.toFilePath()}',
      ],
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
    )¶»§q«^