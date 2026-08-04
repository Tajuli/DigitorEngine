#include "digitor/plugin_package_security.hpp"

#include <iostream>
#include <string>

namespace {
int fail(const char* message) {
  std::cerr << "PLUGIN_PACKAGE_SECURITY_FAILED=" << message << '\n';
  return 1;
}

digitor::PluginPackageInstallRequest valid_request() {
  digitor::PluginPackageInstallRequest request{};
  request.identity.plugin_id = "effect.secure.package";
  request.identity.version = "1.0.0";
  request.identity.publisher_key_id = "digitor.official";
  request.identity.archive_sha256 = std::string(64, 'a');
  request.identity.signature = "signature";
  request.canonical_manifest = "manifest-v1";
  request.archive_bytes = 1024;
  request.entries = {{"manifest.json", 100, 200, false, false, false},
                     {"shaders/windows/effect.dxil", 300, 600, false, false, false}};
  return request;
}
}

int main() {
  using namespace digitor;
  PluginPackageSecurityPolicy policy{};
  std::string diagnostic;

  auto request = valid_request();
  if (!validate_plugin_package(request, policy, diagnostic))
    return fail("valid package was rejected");

  auto traversal = request;
  traversal.entries[0].relative_path = "../escape.bin";
  if (validate_plugin_package(traversal, policy, diagnostic))
    return fail("path traversal was accepted");

  auto bomb = request;
  bomb.archive_bytes = 1;
  bomb.entries[0].uncompressed_size = 1024;
  if (validate_plugin_package(bomb, policy, diagnostic))
    return fail("expansion bomb was accepted");

  auto executable = request;
  executable.entries[0].executable = true;
  if (validate_plugin_package(executable, policy, diagnostic))
    return fail("executable payload was accepted");

  auto revoked = request;
  revoked.identity.revoked = true;
  if (validate_plugin_package(revoked, policy, diagnostic))
    return fail("revoked package was accepted");

  bool verified = false, staged = false, extracted = false;
  bool activated = false, rolled_back = false;
  PluginPackageSecurityBindings bindings{};
  bindings.verify_signature = [&](const PluginPackageIdentity& identity,
                                  std::string_view manifest,
                                  std::string& local) {
    verified = identity.plugin_id == request.identity.plugin_id &&
               manifest == request.canonical_manifest;
    local.clear();
    return verified;
  };
  bindings.create_stage = [&](const PluginPackageIdentity&, std::string& path,
                              std::string& local) {
    staged = true; path = "/plugins/.staging/effect.secure.package";
    local.clear(); return true;
  };
  bindings.extract_to_stage = [&](std::string_view path,
                                  const std::vector<PluginPackageEntry>& entries,
                                  std::string& local) {
    extracted = path.find(".staging") != std::string_view::npos && !entries.empty();
    local.clear(); return extracted;
  };
  bindings.activate_atomically = [&](std::string_view, std::string& path,
                                     std::string& local) {
    activated = true; path = "/plugins/effect.secure.package/1.0.0";
    local.clear(); return true;
  };
  bindings.rollback = [&](std::string_view, std::string_view) { rolled_back = true; };

  const auto installed = install_plugin_package_atomically(request, policy, bindings);
  if (!installed || !verified || !staged || !extracted || !activated || rolled_back)
    return fail("secure atomic install did not complete");

  activated = false;
  bindings.activate_atomically = [&](std::string_view, std::string&,
                                     std::string& local) {
    local = "activation failure"; return false;
  };
  const auto failed = install_plugin_package_atomically(request, policy, bindings);
  if (failed || !rolled_back)
    return fail("failed activation did not roll back");

  std::cout << "PLUGIN_PACKAGE_SECURITY_QUALIFIED=1\n";
  std::cout << "PATH_TRAVERSAL_REJECTED=1\nZIP_BOMB_REJECTED=1\n";
  std::cout << "ATOMIC_ACTIVATION=1\nROLLBACK_VERIFIED=1\n";
  return 0;
}
