@DefaultAsset('package:digitor_engine_ffi/digitor_engine_ffi.dart')
library;

import 'dart:ffi';

import 'package:ffi/ffi.dart';

import 'native_node_graph_api.dart';
import 'native_production_api.dart';

final class DigitorFlutterTimelineSourceNative extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int apiVersion;

  external Pointer<Utf8> sourceMediaGroupId;
  external Pointer<Utf8> path;
}

@Native<
  Int32 Function(
    Pointer<Utf8>,
    IntPtr,
    Pointer<DigitorFlutterTimelineSourceNative>,
    Uint32,
  )
>(symbol: 'digitor_flutter_production_timeline_configure')
external int digitorFlutterProductionTimelineConfigure(
  Pointer<Utf8> serializedProject,
  int serializedSize,
  Pointer<DigitorFlutterTimelineSourceNative> sources,
  int sourceCount,
);

@Native<Int32 Function()>(
  symbol: 'digitor_flutter_production_timeline_configured',
)
external int digitorFlutterProductionTimelineConfigured();

@Native<Int32 Function()>(symbol: 'digitor_flutter_production_timeline_clear')
external int digitorFlutterProductionTimelineClear();

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Uint64,
    Int64,
    Uint32,
    Uint32,
    Pointer<DigitorNativeGpuTextureDescriptorNative>,
  )
>(symbol: 'digitor_flutter_production_timeline_preview')
external int digitorFlutterProductionTimelinePreview(
  Pointer<DigitorNodeGraphNative> graph,
  int graphRevision,
  int parameterRevision,
  int timestampUs,
  int width,
  int height,
  Pointer<DigitorNativeGpuTextureDescriptorNative> outTexture,
);

@Native<Int32 Function(Uint64)>(
  symbol: 'digitor_flutter_production_timeline_preview_consumed',
)
external int digitorFlutterProductionTimelinePreviewConsumed(int generation);

@Native<
  Int32 Function(
    Pointer<DigitorNodeGraphNative>,
    Uint64,
    Uint64,
    Pointer<DigitorFlutterExportRequestV2Native>,
    Pointer<NativeFunction<DigitorProgressNative>>,
    Pointer<Void>,
  )
>(symbol: 'digitor_flutter_production_timeline_export_v2')
external int digitorFlutterProductionTimelineExportV2(
  Pointer<DigitorNodeGraphNative> graph,
  int graphRevision,
  int parameterRevision,
  Pointer<DigitorFlutterExportRequestV2Native> request,
  Pointer<NativeFunction<DigitorProgressNative>> progress,
  Pointer<Void> progressUserData,
);

@Native<Int32 Function()>(symbol: 'digitor_flutter_production_timeline_cancel')
external int digitorFlutterProductionTimelineCancel();

@Native<
  Int32 Function(
    Pointer<Uint8>,
    Pointer<Uint32>,
  )
>(symbol: 'digitor_flutter_production_timeline_get_last_error')
external int digitorFlutterProductionTimelineGetLastError(
  Pointer<Uint8> buffer,
  Pointer<Uint32> size,
);
