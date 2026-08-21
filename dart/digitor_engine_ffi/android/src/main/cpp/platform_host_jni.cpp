#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include "digitor/android_flutter_host_bridge.h"

#include <cstdint>
#include <mutex>

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
  auto* window = reinterpret_cast<ANativeWindow*>(
      static_cast<std::uintptr_t>(handle));
  ANativeWindow_release(window);
}

namespace {

struct AndroidFlutterHostBridgeState {
  std::mutex mutex;
  JavaVM* vm{};
  jobject plugin{};
  jmethodID show_export_progress{};
  jmethodID update_export_progress{};
  jmethodID hide_export_progress{};
};

AndroidFlutterHostBridgeState g_bridge_state;

JNIEnv* acquire_env(JavaVM* vm, bool& attached) noexcept {
  attached = false;
  if (!vm) return nullptr;

  JNIEnv* env = nullptr;
  const auto get_result =
      vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
  if (get_result == JNI_OK) return env;
  if (get_result != JNI_EDETACHED) return nullptr;
  if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return nullptr;
  attached = true;
  return env;
}

void finish_env(JavaVM* vm, bool attached) noexcept {
  if (vm && attached) vm->DetachCurrentThread();
}

void clear_java_exception(JNIEnv* env) noexcept {
  if (env && env->ExceptionCheck()) env->ExceptionClear();
}

struct PluginCallTarget {
  JavaVM* vm{};
  jobject plugin{};
  jmethodID method{};
};

PluginCallTarget make_call_target(
    AndroidFlutterHostBridgeState* state,
    jmethodID AndroidFlutterHostBridgeState::*method_member,
    JNIEnv* env) noexcept {
  PluginCallTarget target{};
  if (!state || !env) return target;
  std::scoped_lock lock(state->mutex);
  target.vm = state->vm;
  target.method = state->*method_member;
  if (state->plugin && target.method) {
    target.plugin = env->NewLocalRef(state->plugin);
    clear_java_exception(env);
  }
  return target;
}

void export_progress_begin(void* user_data) noexcept {
  auto* state = static_cast<AndroidFlutterHostBridgeState*>(user_data);
  JavaVM* vm = nullptr;
  {
    if (!state) return;
    std::scoped_lock lock(state->mutex);
    vm = state->vm;
  }
  bool attached = false;
  JNIEnv* env = acquire_env(vm, attached);
  if (!env) return;
  auto target = make_call_target(
      state, &AndroidFlutterHostBridgeState::show_export_progress, env);
  if (target.plugin && target.method) {
    env->CallVoidMethod(target.plugin, target.method);
    clear_java_exception(env);
    env->DeleteLocalRef(target.plugin);
  }
  finish_env(vm, attached);
}

void export_progress_update(void* user_data, double fraction,
                            int64_t completed, int64_t total) noexcept {
  auto* state = static_cast<AndroidFlutterHostBridgeState*>(user_data);
  JavaVM* vm = nullptr;
  {
    if (!state) return;
    std::scoped_lock lock(state->mutex);
    vm = state->vm;
  }
  bool attached = false;
  JNIEnv* env = acquire_env(vm, attached);
  if (!env) return;
  auto target = make_call_target(
      state, &AndroidFlutterHostBridgeState::update_export_progress, env);
  if (target.plugin && target.method) {
    env->CallVoidMethod(target.plugin, target.method, static_cast<jdouble>(fraction),
                        static_cast<jlong>(completed),
                        static_cast<jlong>(total));
    clear_java_exception(env);
    env->DeleteLocalRef(target.plugin);
  }
  finish_env(vm, attached);
}

void export_progress_end(void* user_data, int32_t result_code) noexcept {
  auto* state = static_cast<AndroidFlutterHostBridgeState*>(user_data);
  JavaVM* vm = nullptr;
  {
    if (!state) return;
    std::scoped_lock lock(state->mutex);
    vm = state->vm;
  }
  bool attached = false;
  JNIEnv* env = acquire_env(vm, attached);
  if (!env) return;
  auto target = make_call_target(
      state, &AndroidFlutterHostBridgeState::hide_export_progress, env);
  if (target.plugin && target.method) {
    env->CallVoidMethod(target.plugin, target.method,
                        static_cast<jint>(result_code));
    clear_java_exception(env);
    env->DeleteLocalRef(target.plugin);
  }
  finish_env(vm, attached);
}

DigitorAndroidFlutterHostBridge g_production_registrar_bridge{
    sizeof(DigitorAndroidFlutterHostBridge),
    DIGITOR_ANDROID_FLUTTER_HOST_BRIDGE_API_VERSION,
    DIGITOR_ANDROID_FLUTTER_HOST_BRIDGE_MAGIC,
    &g_bridge_state,
    &export_progress_begin,
    &export_progress_update,
    &export_progress_end,
};

}  // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_primedigitor_digitor_1engine_1ffi_DigitorEngineFfiPlugin_nativeProductionRegistrarToken(
    JNIEnv* env, jobject thiz) {
  if (!env || !thiz) return 0;

  JavaVM* vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK || !vm) return 0;

  jclass clazz = env->GetObjectClass(thiz);
  if (!clazz) {
    clear_java_exception(env);
    return 0;
  }
  const auto show = env->GetMethodID(
      clazz, "showExportProgressFromNative", "()V");
  const auto update = env->GetMethodID(
      clazz, "updateExportProgressFromNative", "(DJJ)V");
  const auto hide = env->GetMethodID(
      clazz, "hideExportProgressFromNative", "(I)V");
  env->DeleteLocalRef(clazz);
  if (!show || !update || !hide || env->ExceptionCheck()) {
    clear_java_exception(env);
    return 0;
  }

  jobject global_plugin = env->NewGlobalRef(thiz);
  if (!global_plugin) {
    clear_java_exception(env);
    return 0;
  }

  {
    std::scoped_lock lock(g_bridge_state.mutex);
    if (g_bridge_state.plugin) {
      env->DeleteGlobalRef(g_bridge_state.plugin);
    }
    g_bridge_state.vm = vm;
    g_bridge_state.plugin = global_plugin;
    g_bridge_state.show_export_progress = show;
    g_bridge_state.update_export_progress = update;
    g_bridge_state.hide_export_progress = hide;
  }

  return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(
      &g_production_registrar_bridge));
}

extern "C" JNIEXPORT void JNICALL
Java_com_primedigitor_digitor_1engine_1ffi_DigitorEngineFfiPlugin_nativeReleaseProductionRegistrarToken(
    JNIEnv* env, jobject /*thiz*/) {
  if (!env) return;
  std::scoped_lock lock(g_bridge_state.mutex);
  if (g_bridge_state.plugin) {
    env->DeleteGlobalRef(g_bridge_state.plugin);
  }
  g_bridge_state.plugin = nullptr;
  g_bridge_state.show_export_progress = nullptr;
  g_bridge_state.update_export_progress = nullptr;
  g_bridge_state.hide_export_progress = nullptr;
  g_bridge_state.vm = nullptr;
}
