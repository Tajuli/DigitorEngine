// Aggregated by native_still_image_host.cpp so the established DigitorEngine
// target wiring remains stable across package configurations.
#if defined(_WIN32)
#include "windows_wic_image_codec.cpp"
#elif defined(__ANDROID__)
#include "android_image_codec.cpp"
#endif
