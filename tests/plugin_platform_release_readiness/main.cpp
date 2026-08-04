#include "digitor/plugin_platform_release_readiness.hpp"

#include <iostream>

int main() {
  using namespace digitor;
  PluginReleaseReadinessInput input;
  input.engine_version = "4.9.0";
  input.components = {
      {"marketplace", "1", true, true, true},
      {"authoring-sdk", "1", true, true, true},
      {"distribution", "1", true, true, true},
      {"flutter-app-sdk", "1", true, true, true},
      {"project-runtime", "1", true, true, true}};
  input.platforms = {
      {"windows", true, true, true, true, true},
      {"android", true, true, true, true, true},
      {"macos", true, true, true, true, true},
      {"ios", true, true, true, true, true}};
  input.filter_code_free = true;
  input.effect_code_free = true;
  input.transition_code_free = true;
  input.exact_version_pinning = true;
  input.signed_atomic_installation = true;
  input.preview_export_parity = true;

  const auto ready = evaluate_plugin_platform_release_readiness(input);
  if (!ready.ready || !ready.findings.empty()) return 1;

  input.policy.paid_export_requires_app_authorization = false;
  const auto invalid_policy = evaluate_plugin_platform_release_readiness(input);
  if (invalid_policy.ready) return 2;

  input.policy.paid_export_requires_app_authorization = true;
  input.platforms.pop_back();
  const auto missing_ios = evaluate_plugin_platform_release_readiness(input);
  if (missing_ios.ready) return 3;

  input.platforms.push_back({"ios", true, true, true, true, true});
  input.main_rendering_features_changed = true;
  const auto renderer_regression =
      evaluate_plugin_platform_release_readiness(input);
  if (renderer_regression.ready) return 4;

  std::cout << "PLUGIN_PLATFORM_RELEASE_READY=1\n"
               "WINDOWS_ANDROID_MACOS_IOS=1\n"
               "CODE_FREE_FILTER_EFFECT_TRANSITION=1\n"
               "APP_OWNS_COMMERCIAL_POLICY=1\n"
               "PAID_PREVIEW_FOR_FREE_USER=1\n"
               "PAID_EXPORT_REQUIRES_APP_AUTHORIZATION=1\n"
               "MAIN_RENDERING_FEATURES_CHANGED=0\n";
  return 0;
}
