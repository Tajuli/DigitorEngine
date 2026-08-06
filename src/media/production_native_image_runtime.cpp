// Reserved aggregation seam for the app-facing native image session. The
// concrete codecs and deterministic CPU transforms are compiled above through
// native_image_runtime.cpp; GPU upload/process/export remains owned by the
// locked GpuImageSession host so this layer cannot introduce CPU fallback after
// GPU selection.
