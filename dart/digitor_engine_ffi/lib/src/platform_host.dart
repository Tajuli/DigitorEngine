import 'dart:async';

import 'package:flutter/services.dart';

import 'engine.dart';
import 'production.dart';

/// Flutter embedding capabilities reported by the platform texture host.
final class DigitorFlutterHostCapabilities {
  const DigitorFlutterHostCapabilities({
    required this.platform,
    required this.supportedHandleTypes,
    required this.directDescriptorPresentation,
    required this.renderTargetPresentation,
  });

  final String platform;
  final Set<DigitorNativeTextureHandleType> supportedHandleTypes;
  final bool directDescriptorPresentation;
  final bool renderTargetPresentation;

  bool supports(DigitorNativeTextureHandleType type) =>
      supportedHandleTypes.contains(type);
}

/// A Flutter texture registered by the native platform host.
final class DigitorFlutterTextureTarget {
  const DigitorFlutterTextureTarget({
    required this.textureId,
    required this.nativeTargetHandle,
    required this.targetKind,
    required this.requestedHandleType,
  });

  final int textureId;

  /// Opaque platform target. On Android this is an ANativeWindow pointer owned
  /// by the SurfaceProducer host. It is never CPU-addressable memory.
  final int nativeTargetHandle;
  final String targetKind;
  final DigitorNativeTextureHandleType requestedHandleType;
}

final class DigitorFlutterRenderTargetChange {
  const DigitorFlutterRenderTargetChange({
    required this.textureId,
    required this.nativeTargetHandle,
    required this.targetKind,
    required this.available,
  });

  final int textureId;
  final int nativeTargetHandle;
  final String targetKind;
  final bool available;
}

/// Production Flutter texture-registry host for DigitorEngine.
///
/// Windows consumes DXGI shared handles or D3D11 textures directly. Apple
/// consumes CVPixelBuffer-backed frames. Android owns a SurfaceProducer and
/// exposes its ANativeWindow as an opaque render target so the selected Vulkan
/// or GLES path can render without a Dart/CPU pixel copy.
final class DigitorFlutterPlatformHost {
  DigitorFlutterPlatformHost({MethodChannel? channel})
    : _channel = channel ?? const MethodChannel(channelName) {
    _channel.setMethodCallHandler(_handlePlatformCall);
  }

  static const String channelName = 'digitor_engine_ffi/platform_host';

  final MethodChannel _channel;
  final StreamController<DigitorFlutterRenderTargetChange> _targetChanges =
      StreamController<DigitorFlutterRenderTargetChange>.broadcast();
  bool _closed = false;

  Stream<DigitorFlutterRenderTargetChange> get renderTargetChanges =>
      _targetChanges.stream;

  Future<Object?> _handlePlatformCall(MethodCall call) async {
    if (call.method != 'renderTargetChanged' ||
        call.arguments is! Map<Object?, Object?>) {
      return null;
    }
    final args = call.arguments as Map<Object?, Object?>;
    final textureId = args['textureId'];
    final nativeTargetHandle = args['nativeTargetHandle'];
    if (!_closed && textureId is int && nativeTargetHandle is int) {
      _targetChanges.add(
        DigitorFlutterRenderTargetChange(
          textureId: textureId,
          nativeTargetHandle: nativeTargetHandle,
          targetKind: args['targetKind'] as String? ?? 'unknown',
          available: args['available'] as bool? ?? nativeTargetHandle != 0,
        ),
      );
    }
    return null;
  }

  Future<DigitorFlutterHostCapabilities> capabilities() async {
    final value = await _channel.invokeMapMethod<String, Object?>(
      'capabilities',
    );
    if (value == null) {
      throw StateError('Flutter platform host returned no capabilities.');
    }
    final rawTypes = value['supportedHandleTypes'];
    final types = <DigitorNativeTextureHandleType>{};
    if (rawTypes is List) {
      for (final raw in rawTypes) {
        if (raw is int) {
          final type = DigitorNativeTextureHandleType.fromNative(raw);
          if (type != DigitorNativeTextureHandleType.none &&
              type != DigitorNativeTextureHandleType.cpuPointer) {
            types.add(type);
          }
        }
      }
    }
    return DigitorFlutterHostCapabilities(
      platform: value['platform'] as String? ?? 'unknown',
      supportedHandleTypes: Set.unmodifiable(types),
      directDescriptorPresentation:
          value['directDescriptorPresentation'] as bool? ?? false,
      renderTargetPresentation:
          value['renderTargetPresentation'] as bool? ?? false,
    );
  }

  Future<DigitorFlutterTextureTarget> createTexture({
    required DigitorNativeTextureHandleType handleType,
    required int width,
    required int height,
  }) async {
    if (handleType == DigitorNativeTextureHandleType.none ||
        handleType == DigitorNativeTextureHandleType.cpuPointer) {
      throw ArgumentError.value(handleType, 'handleType');
    }
    if (width <= 0) throw ArgumentError.value(width, 'width');
    if (height <= 0) throw ArgumentError.value(height, 'height');

    final value = await _channel.invokeMapMethod<String, Object?>(
      'createTexture',
      <String, Object>{
        'handleType': handleType.nativeValue,
        'width': width,
        'height': height,
      },
    );
    return _targetFromMap(value, handleType);
  }

  /// Refreshes an Android SurfaceProducer target after a surface lifecycle
  /// change. Descriptor-driven desktop/Apple hosts need no refresh.
  Future<DigitorFlutterTextureTarget> refreshTextureTarget(
    DigitorFlutterTextureTarget target,
  ) async {
    final value = await _channel.invokeMapMethod<String, Object?>(
      'refreshTextureTarget',
      <String, Object>{'textureId': target.textureId},
    );
    return _targetFromMap(value, target.requestedHandleType);
  }

  DigitorFlutterTextureTarget _targetFromMap(
    Map<String, Object?>? value,
    DigitorNativeTextureHandleType handleType,
  ) {
    if (value == null) {
      throw StateError('Flutter platform host failed to return a texture.');
    }
    final textureId = value['textureId'];
    if (textureId is! int || textureId < 0) {
      throw StateError('Flutter platform host returned an invalid texture id.');
    }
    return DigitorFlutterTextureTarget(
      textureId: textureId,
      nativeTargetHandle: value['nativeTargetHandle'] as int? ?? 0,
      targetKind: value['targetKind'] as String? ?? 'unknown',
      requestedHandleType: handleType,
    );
  }

  /// Publishes one strict native GPU frame to a descriptor-driven host.
  ///
  /// Android SurfaceProducer hosts are render-target driven. Their engine-side
  /// presenter renders into [DigitorFlutterTextureTarget.nativeTargetHandle]
  /// and calls [markFrameAvailable] instead of importing a Dart descriptor.
  Future<void> present(
    DigitorFlutterTextureTarget target,
    DigitorNativeGpuTextureFrame frame,
  ) async {
    if (frame.nativeHandle == 0 ||
        frame.generation == 0 ||
        frame.readiness != DigitorNativeTextureReadiness.ready ||
        frame.handleType == DigitorNativeTextureHandleType.none ||
        frame.handleType == DigitorNativeTextureHandleType.cpuPointer ||
        frame.backend == DigitorNativeTextureBackend.cpuRgba8) {
      throw ArgumentError('Only a ready native GPU frame can be presented.');
    }
    await _channel.invokeMethod<void>('present', <String, Object>{
      'textureId': target.textureId,
      ...descriptorArguments(frame),
    });
  }

  Future<void> markFrameAvailable(
    DigitorFlutterTextureTarget target, {
    required int generation,
  }) async {
    if (generation <= 0) {
      throw ArgumentError.value(generation, 'generation');
    }
    await _channel.invokeMethod<void>('markFrameAvailable', <String, Object>{
      'textureId': target.textureId,
      'generation': generation,
    });
  }

  Future<void> disposeTexture(DigitorFlutterTextureTarget target) =>
      _channel.invokeMethod<void>('disposeTexture', <String, Object>{
        'textureId': target.textureId,
      });

  Future<void> close() async {
    if (_closed) return;
    _closed = true;
    _channel.setMethodCallHandler(null);
    await _targetChanges.close();
  }

  static Map<String, Object> descriptorArguments(
    DigitorNativeGpuTextureFrame frame,
  ) => <String, Object>{
    'backend': frame.backend.nativeValue,
    'handleType': frame.handleType.nativeValue,
    'nativeHandle': frame.nativeHandle,
    'secondaryHandle': frame.secondaryHandle,
    'width': frame.width,
    'height': frame.height,
    'pixelFormat': frame.pixelFormat.nativeValue,
    'alphaMode': frame.alphaMode,
    'colorPrimaries': frame.colorPrimaries,
    'transferFunction': frame.transferFunction,
    'matrixCoefficients': frame.matrixCoefficients,
    'colorRange': frame.colorRange,
    'timestampUs': frame.timestampUs,
    'generation': frame.generation,
    'deviceIdentity': frame.deviceIdentity,
    'contextIdentity': frame.contextIdentity,
    'acquireSyncHandle': frame.acquireSyncHandle,
    'acquireSyncValue': frame.acquireSyncValue,
    'releaseSyncHandle': frame.releaseSyncHandle,
    'releaseSyncValue': frame.releaseSyncValue,
    'ownershipToken': frame.ownershipToken,
    'protectedContent': frame.protectedContent,
    'readiness': frame.readiness.nativeValue,
  };
}
