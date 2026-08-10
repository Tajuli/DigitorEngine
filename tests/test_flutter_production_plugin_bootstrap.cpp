#include "digitor/flutter_production_plugin_bootstrap.hpp"

#include <cassert>
#include <memory>

int main() {
  using namespace digitor;

  int registrar_token = 7;
  DigitorFlutterProductionPluginAttachment attachment{};
  attachment.struct_size = sizeof(attachment);
  attachment.api_version = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_ATTACHMENT_VERSION;
  attachment.platform = DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS;
  attachment.flutter_texture_registrar = &registrar_token;
  attachment.implementation_identity = "test.flutter.windows";

  assert(digitor_flutter_production_plugin_attach(&attachment) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  assert(digitor_flutter_production_plugin_attached() == 0);

  const auto installed = install_flutter_production_host_inputs_factory(
      DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS,
      [](const FlutterProductionPluginAttachment& input, std::string& diagnostic)
          -> std::optional<FlutterProductionHostAdapterInputs> {
        assert(input.flutter_texture_registrar != nullptr);
        FlutterProductionHostAdapterInputs values{};
        values.decoder_factory = [](const std::string&, std::string& d)
            -> std::unique_ptr<ProductionHardwareDecodeSession> {
          d = "test factory intentionally has no decoder session";
          return nullptr;
        };
        values.frame_resolver = [](std::int64_t timestamp_us) {
          return static_cast<FrameNumber>(timestamp_us / 33333);
        };
        diagnostic.clear();
        return values;
      });
  assert(installed == DIGITOR_RESULT_OK);
  assert(digitor_flutter_production_plugin_attach(&attachment) ==
         DIGITOR_RESULT_NOT_INITIALIZED);
  assert(digitor_flutter_production_plugin_attached() == 0);
  assert(clear_flutter_production_host_inputs_factory(
             DIGITOR_FLUTTER_PRODUCTION_PLUGIN_WINDOWS) == DIGITOR_RESULT_OK);

  DigitorFlutterProductionPluginAttachment invalid{};
  assert(digitor_flutter_production_plugin_attach(&invalid) ==
         DIGITOR_RESULT_INVALID_ARGUMENT);
  assert(digitor_flutter_production_plugin_detach(&registrar_token) ==
         DIGITOR_RESULT_OK);
  return 0;
}
