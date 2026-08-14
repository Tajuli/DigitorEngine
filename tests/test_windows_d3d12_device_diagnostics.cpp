#include "gpu/windows_d3d12_device_diagnostics_internal.hpp"

#include <cassert>

int main() {
  digitor::internal::DeviceRemovalCheckpointTracker tracker;
  assert(!tracker.observe("resource.open", 0));
  assert(!tracker.observe("fence.open.before", 0));
  assert(tracker.observe("srv.uv", static_cast<std::int32_t>(0x887A0005u)));
  assert(tracker.first_failure() == "srv.uv");
  assert(tracker.last_healthy() == "fence.open.before");
  assert(tracker.observe("retry", static_cast<std::int32_t>(0x887A0005u)));
  assert(tracker.first_failure() == "srv.uv");
  return 0;
}
