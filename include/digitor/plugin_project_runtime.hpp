#ifndef DIGITOR_PLUGIN_PROJECT_RUNTIME_HPP
#define DIGITOR_PLUGIN_PROJECT_RUNTIME_HPP
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
namespace digitor {
enum class PluginProjectKind : std::uint32_t { filter, effect, transition };
enum class PluginProjectState : std::uint32_t { ready, missing, incompatible, revoked, disabled };
struct PluginProjectInstance {
  std::string instance_id;
  std::string plugin_id;
  std::string version;
  std::string package_sha256;
  PluginProjectKind kind{PluginProjectKind::filter};
  std::unordered_map<std::string,double> numeric_parameters;
  bool enabled{true};
};
struct PluginInstalledIdentity { std::string plugin_id, version, package_sha256; bool compatible{true}; bool revoked{false}; };
struct PluginProjectResolution { PluginProjectState state{PluginProjectState::missing}; std::string diagnostic; };
class PluginProjectRuntime final {
 public:
  bool load_snapshot(const std::string& text, std::string& diagnostic);
  std::string save_snapshot() const;
  bool activate(const std::string& instance_id, const std::vector<PluginInstalledIdentity>& installed, PluginProjectResolution& out);
  bool deactivate(const std::string& instance_id);
  bool set_numeric_parameter(const std::string& instance_id, const std::string& parameter_id, double value);
  const std::vector<PluginProjectInstance>& instances() const noexcept { return instances_; }
 private:
  std::vector<PluginProjectInstance> instances_;
};
}
#endif
