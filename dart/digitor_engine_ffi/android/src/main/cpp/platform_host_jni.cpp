#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <cstdint>

extern "C" JNIEXPORT jlong JNICALL
Java_com_primedigitor_digitor_1engine_1ffi_DigitorEngineFfiPlugin_nativeAcquireWindow(
    JNIEnv* env, jobject /*thiz*/, jobject surface) {
  if (!env || !surface) return 0;
  ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(window));
}

extern "C" JNIEXPORT void JNICALL
Java_com_primedigitor_digitor_1engine_1ffi_DigitorEngineFfiPlugin_nativeReleaseWindow(
    JNIEnv* /*env*/, jobject /*thiz*/, jlong handle) {
  if (!handle) return;
  auto* window = reinterpret_cast<ANativeWindow*>(static_cast<std::uintptr_t>(handle));
  ANativeWindow_release(window);
}
