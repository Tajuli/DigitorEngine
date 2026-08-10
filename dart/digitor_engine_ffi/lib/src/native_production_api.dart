@DefaultAsset('package:digitor_engine_ffi/digitor_engine_ffi.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'native_api.dart';
import 'native_node_graph_api.dart';

final class DigitorFlutterProductionSessionNative extends Opaque {}

final class DigitorNativeGpuTextureDescriptorNative extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  @Int32()
  external int backend;
  @Int32()
  external int handleType;
  @Uint64()
  external int nativeHandle;
  @Uint64()
  external int secondaryHandle;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Int32()
  external int pixelFormat;
  @Uint32()
  external int alphaMode;
  @Uint32()
  external int colorPrimaries;
  @Uint32()
  external int transferFunction;
  @Uint32()
  external int matrixCoefficients;
  @Uint32()
  external int colorRange;
  @Int64()
  external int timestampUs;
  @Uint64()
  external int generation;
  @Uint64()
  external int deviceIdentity;
  @Uint64()
  external int contextIdentity;
  @Uint64()
  external int acquireSyncHandle;
  @Uint64()
  external int acquireSyncValue;
  @Uint64()
  external int releaseSyncHandle;
  @Uint64()
  external int releaseSyncValue;
  @Uint64()
  external int ownershipToken;
  @Uint8()
  external int protectedContent;
  @Int32()
  external int readiness;
}

final class DigitorFlutterPreviewTargetNative extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  @Uint64()
  external int nativeTargetHandle;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Int32()
  external int handleType;
}

final class DigitorFlutterExportRequestNative extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  external Pointer<Utf8> outputPath;
  @Int32()
  external int format;
  @Int32()
  external int codec;
  @Int64()
  external int firstFrame;
  @Int64()
  external int lastFrame;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
}

final class DigitorFlutterExportRequestV2Native extends Struct {
  @Uint32() external int structSize;
  @Uint32() external int apiVersion;
  external Pointer<Utf8> outputPath;
  @Int32() external int format;
  @Int32() external int codec;
  @Int64() external int firstFrame;
  @Int64() external int lastFrame;
  @Uint32() external int width;
  @Uint32() external int height;
  @Uint64() external int snapshotIdentity;
  @Uint64() external int timelineRevision;
  @Uint64() external int renderRevision;
  @Uint64() external int graphRevision;
  @Uint64() external int parameterRevision;
  @Uint64() external int audioRevision;
  @Int32() external int workingPixelFormat;
  @Int32() external int alphaPolicy;
  @Uint8() external int variableFrameRate;
  @Uint8() external int hdr;
  @Array(6) external Array<Uint8> reserved;
  external Pointer<Utf8> colorMetadata;
  @Int32() external int rendererBackend;
  @Int32() external int encoderBackend;
  @Uint64() external int deviceIdentity;
  @Uint64() external int contextIdentity;
}

typedef DigitorFlutterOpenMediaNative =
    Int32 Function(Pointer<Void>, Pointer<Utf8>, Pointer<Uint8>, Uint32);

typedef DigitorFlutterRenderFrameNative =
    Int32 Function(
      Pointer<Void>,
      Int32,
      Pointer<DigitorNodeGraphNative>,
      Uint64,
      Uint64,
      Int64,
      Uint32,
      Uint32,
      Pointer<DigitorNativeGpuTextureDescriptorNative>,
      Pointer<Uint8>,
      Uint32,
    );

typedef DigitorFlutterExportMediaNative =
    Int32 Function(
      Pointer<Void>,
      Pointer<DigitorNodeGraphNative>,
      Uint64,
      Uint64,
      Pointer<DigitorFlutterExportRequestNative>,
      Pointer<NativeFunction<DigitorProgressNative>>,
      Pointer<Void>,
      Pointer<Uint8>,
      Uint32,
    );

typedef DigitorFlutterSetPreviewTargetNative =
    Int32 Function(
      Pointer<Void>,
      Pointer<DigitorFlutterPreviewTargetNative>,
      Pointer<Uint8>,
      Uint32,
    );

typedef DigitorFlutterQueryPreviewNative =
    Int32 Function(
      Pointer<Void>,
      Pointer<DigitorNativePreviewCapabilitiesNative>,
    );

typedef DigitorFlutterCancelNative = Int32 Function(Pointer<Void>);
typedef DigitorFlutterCloseMediaNative = Void Function(Pointer<Void>);
typedef DigitorFlutterReleaseTextureNative =
    Void Function(
      Pointer<Void>,
      Pointer<DigitorNativeGpuTextureDescriptorNative>,
    );

final class DigitorFlutterProductionHostNative extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  external Pointer<Void> userData;
  @Uint64()
  external int requiredDeviceIdentity;
  @Uint64()
  external int requiredContextIdentity;
  external Pointer<NativeFunction<DigitorFlutterOpenMediaNative>> openMedia;
  external Pointer<NativeFunction<DigitorFlutterRenderFrameNative>> renderFrame;
  external Pointer<NativeFunction<DigitorFlutterExportMediaNative>> exportMedia;
  external Pointer<NativeFunction<DigitorFlutterQueryPreviewNative>>
  queryPreview;
  external Pointer<NativeFunction<DigitorFlutterSetPreviewTargetNative>>
  setPreviewTarget;
  external Pointer<NativeFunction<DigitorFlutterCancelNative>> cancel;
  external Pointer<NativeFunction<DigitorFlutterCloseMediaNative>> closeMedia;
  external Pointer<NativeFunction<DigitorFlutterReleaseTextureNative>>
  releaseTexture;
}

@Native<Int32 Function()>(symbol: 'digitor_flutter_production_host_registered')
external int digitorFlutterProductionHostRegistered();

final class DigitorFlutterProductionPluginAttachmentNative extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  @Uint32()
  external int platform;
  external Pointer<Void> flutterTextureRegistrar;
  external Pointer<Utf8> implementationIdentity;
}

@Native<
  Int32 Function(Pointer<DigitorFlutterProductionPluginAttachmentNative>)
>(symbol: 'digitor_flutter_production_plugin_attach')
external int digitorFlutterProductionPluginAttach(
  Pointer<DigitorFlutterProductionPluginAttachmentNative> attachment,
);

@Native<Int32 Function(Pointer<Void>)>(
  symbol: 'digitor_flutter_production_plugin_detach',
)
external int digitorFlutterProductionPluginDetach(Pointer<Void> registrar);

@Native<Uint8 Function()>(symbol: 'digitor_flutter_production_plugin_attached')
external int digitorFlutterProductionPluginAttached();

@Native<
  Int32 Function(
    Pointer<Utf8>,
    Pointer<Pointer<DigitorFlutterProductionSessionNative>>,
  )
>(symbol: 'digitor_flutter_production_create_registered')
external int digitorFlutterProductionCreateRegistered(
  Pointer<Utf8> mediaPath,
  Pointer<Pointer<DigitorFlutterProductionSessionNative>> outSession,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionHostNative>,
    Pointer<Utf8>,
    Pointer<Pointer<DigitorFlutterProductionSessionNative>>,
  )
>(symbol: 'digitor_flutter_production_create')
external int digitorFlutterProductionCreate(
  Pointer<DigitorFlutterProductionHostNative> host,
  Pointer<Utf8> mediaPath,
  Pointer<Pointer<DigitorFlutterProductionSessionNative>> outSession,
);

@Native<Int32 Function(Pointer<DigitorFlutterProductionSessionNative>)>(
  symbol: 'digitor_flutter_production_destroy',
)
external int digitorFlutterProductionDestroy(
  Pointer<DigitorFlutterProductionSessionNative> session,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Uint64,
  )
>(symbol: 'digitor_flutter_production_bind_node_graph')
external int digitorFlutterProductionBindNodeGraph(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<DigitorNodeGraphNative> graph,
  int graphRevision,
  int parameterRevision,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Pointer<DigitorFlutterPreviewTargetNative>,
  )
>(symbol: 'digitor_flutter_production_set_preview_target')
external int digitorFlutterProductionSetPreviewTarget(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<DigitorFlutterPreviewTargetNative> target,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Int64,
    Uint32,
    Uint32,
    Pointer<DigitorNativeGpuTextureDescriptorNative>,
  )
>(symbol: 'digitor_flutter_production_preview')
external int digitorFlutterProductionPreview(
  Pointer<DigitorFlutterProductionSessionNative> session,
  int timestampUs,
  int width,
  int height,
  Pointer<DigitorNativeGpuTextureDescriptorNative> outTexture,
);

@Native<Int32 Function(Pointer<DigitorFlutterProductionSessionNative>, Uint64)>(
  symbol: 'digitor_flutter_production_preview_consumed',
)
external int digitorFlutterProductionPreviewConsumed(
  Pointer<DigitorFlutterProductionSessionNative> session,
  int generation,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Pointer<DigitorNativePreviewCapabilitiesNative>,
  )
>(symbol: 'digitor_flutter_production_query_preview')
external int digitorFlutterProductionQueryPreview(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<DigitorNativePreviewCapabilitiesNative> outCapabilities,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Pointer<DigitorFlutterExportRequestNative>,
    Pointer<NativeFunction<DigitorProgressNative>>,
    Pointer<Void>,
  )
>(symbol: 'digitor_flutter_production_export')
external int digitorFlutterProductionExport(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<DigitorFlutterExportRequestNative> request,
  Pointer<NativeFunction<DigitorProgressNative>> progress,
  Pointer<Void> progressUserData,
);

@Native<Int32 Function(Pointer<DigitorFlutterProductionSessionNative>, Pointer<DigitorFlutterExportRequestV2Native>, Pointer<NativeFunction<DigitorProgressNative>>, Pointer<Void>)>(symbol: 'digitor_flutter_production_export_v2')
external int digitorFlutterProductionExportV2(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<DigitorFlutterExportRequestV2Native> request,
  Pointer<NativeFunction<DigitorProgressNative>> progress,
  Pointer<Void> progressUserData,
);

@Native<Int32 Function(Pointer<DigitorFlutterProductionSessionNative>)>(
  symbol: 'digitor_flutter_production_cancel',
)
external int digitorFlutterProductionCancel(
  Pointer<DigitorFlutterProductionSessionNative> session,
);

@Native<
  Int32 Function(
    Pointer<DigitorFlutterProductionSessionNative>,
    Pointer<Uint8>,
    Pointer<Uint32>,
  )
>(symbol: 'digitor_flutter_production_get_last_error')
external int digitorFlutterProductionGetLastError(
  Pointer<DigitorFlutterProductionSessionNative> session,
  Pointer<Uint8> buffer,
  Pointer<Uint32> size,
);
