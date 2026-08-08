@DefaultAsset('package:digitor_engine_ffi/digitor_engine_ffi.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

final class DigitorEngineConfigNative extends Struct {
  @Int32()
  external int preferredBackend;

  @Uint8()
  external int enableValidation;

  @Uint8()
  external int allowCpuFallback;
}

final class DigitorRendererInfoNative extends Struct {
  @Int32()
  external int backend;

  @Array(64)
  external Array<Int8> backendName;

  @Array(128)
  external Array<Int8> deviceName;

  @Uint8()
  external int isGpu;

  @Uint8()
  external int supportsCompute;

  @Uint8()
  external int supportsFp16;

  @Uint8()
  external int supportsFp32;
}

final class DigitorColorControlsNative extends Struct {
  @Float()
  external double exposure;

  @Float()
  external double contrast;

  @Float()
  external double saturation;
}

final class DigitorSdkSessionNative extends Opaque {}

final class DigitorNativeTextureNative extends Struct {
  external Pointer<Void> pixels;

  @Uint32()
  external int width;

  @Uint32()
  external int height;

  @Uint32()
  external int rowBytes;

  @Uint64()
  external int generation;
}

final class DigitorNativePreviewCapabilitiesNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  @Uint8()
  external int nativeGpuPreviewAvailable;

  @Uint8()
  external int trueSharedResourceZeroCopy;

  @Uint8()
  external int gpuToGpuCopy;

  @Uint8()
  external int cpuFallbackOnly;

  @Uint8()
  external int sdrSupported;

  @Uint8()
  external int hdrSupported;

  @Uint8()
  external int protectedContentSupported;

  @Uint8()
  external int resizeSupported;

  @Int32()
  external int backend;

  @Int32()
  external int handleType;

  @Uint64()
  external int supportedPixelFormats;

  @Int32()
  external int selectedMode;

  @Array(192)
  external Array<Int8> reasonUnavailable;
}

typedef DigitorCompletionNative = Void Function(Int32, Pointer<Void>);
typedef DigitorProgressNative =
    Void Function(Double, Int64, Int64, Pointer<Void>);

@Native<Pointer<Utf8> Function()>(symbol: 'digitor_get_version')
external Pointer<Utf8> digitorGetVersion();

@Native<Int32 Function(Pointer<DigitorEngineConfigNative>)>(
  symbol: 'digitor_initialize',
)
external int digitorInitialize(Pointer<DigitorEngineConfigNative> config);

@Native<Int32 Function()>(symbol: 'digitor_shutdown')
external int digitorShutdown();

@Native<Int32 Function(Pointer<DigitorRendererInfoNative>)>(
  symbol: 'digitor_get_renderer_info',
)
external int digitorGetRendererInfo(Pointer<DigitorRendererInfoNative> outInfo);

@Native<Int32 Function(Pointer<Pointer<DigitorSdkSessionNative>>)>(
  symbol: 'digitor_sdk_create',
)
external int digitorSdkCreate(
  Pointer<Pointer<DigitorSdkSessionNative>> outSession,
);

@Native<Int32 Function(Pointer<DigitorSdkSessionNative>)>(
  symbol: 'digitor_sdk_destroy',
)
external int digitorSdkDestroy(Pointer<DigitorSdkSessionNative> session);

@Native<
  Int32 Function(Pointer<DigitorSdkSessionNative>, DigitorColorControlsNative)
>(symbol: 'digitor_sdk_set_color')
external int digitorSdkSetColor(
  Pointer<DigitorSdkSessionNative> session,
  DigitorColorControlsNative controls,
);

@Native<
  Int32 Function(
    Pointer<DigitorSdkSessionNative>,
    Int64,
    Uint32,
    Uint32,
    Pointer<NativeFunction<DigitorCompletionNative>>,
    Pointer<Void>,
  )
>(symbol: 'digitor_sdk_preview_async')
external int digitorSdkPreviewAsync(
  Pointer<DigitorSdkSessionNative> session,
  int frame,
  int width,
  int height,
  Pointer<NativeFunction<DigitorCompletionNative>> callback,
  Pointer<Void> userData,
);

@Native<
  Int32 Function(
    Pointer<DigitorSdkSessionNative>,
    Int64,
    Pointer<NativeFunction<DigitorCompletionNative>>,
    Pointer<Void>,
  )
>(symbol: 'digitor_sdk_seek_async')
external int digitorSdkSeekAsync(
  Pointer<DigitorSdkSessionNative> session,
  int frame,
  Pointer<NativeFunction<DigitorCompletionNative>> callback,
  Pointer<Void> userData,
);

@Native<
  Int32 Function(
    Pointer<DigitorSdkSessionNative>,
    Pointer<DigitorNativeTextureNative>,
  )
>(symbol: 'digitor_sdk_get_native_texture')
external int digitorSdkGetNativeTexture(
  Pointer<DigitorSdkSessionNative> session,
  Pointer<DigitorNativeTextureNative> outTexture,
);

@Native<Int32 Function(Pointer<DigitorSdkSessionNative>, Int32)>(
  symbol: 'digitor_sdk_set_preview_mode',
)
external int digitorSdkSetPreviewMode(
  Pointer<DigitorSdkSessionNative> session,
  int mode,
);

@Native<
  Int32 Function(
    Pointer<DigitorSdkSessionNative>,
    Pointer<DigitorNativePreviewCapabilitiesNative>,
  )
>(symbol: 'digitor_sdk_query_native_preview')
external int digitorSdkQueryNativePreview(
  Pointer<DigitorSdkSessionNative> session,
  Pointer<DigitorNativePreviewCapabilitiesNative> outCapabilities,
);

@Native<
  Int32 Function(
    Pointer<DigitorSdkSessionNative>,
    Pointer<Utf8>,
    Int32,
    Int32,
    Int64,
    Int64,
    Uint32,
    Uint32,
    Pointer<NativeFunction<DigitorProgressNative>>,
    Pointer<NativeFunction<DigitorCompletionNative>>,
    Pointer<Void>,
  )
>(symbol: 'digitor_sdk_export_async')
external int digitorSdkExportAsync(
  Pointer<DigitorSdkSessionNative> session,
  Pointer<Utf8> path,
  int format,
  int codec,
  int first,
  int last,
  int width,
  int height,
  Pointer<NativeFunction<DigitorProgressNative>> progress,
  Pointer<NativeFunction<DigitorCompletionNative>> completion,
  Pointer<Void> userData,
);

@Native<Int32 Function(Pointer<DigitorSdkSessionNative>)>(
  symbol: 'digitor_sdk_cancel',
)
external int digitorSdkCancel(Pointer<DigitorSdkSessionNative> session);
