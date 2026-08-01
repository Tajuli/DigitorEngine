#include "digitor/live_playback_sync_c_api.h"
#include "digitor/live_playback_sync.hpp"

#include <new>

struct DigitorLivePlaybackSync { digitor::LivePlaybackSync value; explicit DigitorLivePlaybackSync(int64_t o, bool m) : value(o, m) {} };

extern "C" {
DigitorLivePlaybackSync* digitor_live_playback_sync_create(int64_t o, int32_t m) { try { return new (std::nothrow) DigitorLivePlaybackSync(o, m != 0); } catch (...) { return nullptr; } }
void digitor_live_playback_sync_destroy(DigitorLivePlaybackSync* h) { delete h; }
void digitor_live_playback_sync_set_manual(DigitorLivePlaybackSync* h, int64_t o, int32_t m) { if (h) h->value.set_manual_offset(o, m != 0); }
void digitor_live_playback_sync_notify_device_change(DigitorLivePlaybackSync* h) { if (h) h->value.notify_audio_device_changed(); }
int32_t digitor_live_playback_sync_refresh(DigitorLivePlaybackSync* h, int64_t now) { return h && h->value.refresh_probe(now) ? 1 : 0; }
int64_t digitor_live_playback_sync_clock(DigitorLivePlaybackSync* h, int64_t raw) { return h ? h->value.compensated_clock_us(raw) : raw; }
int32_t digitor_live_playback_sync_snapshot(DigitorLivePlaybackSync* h, DigitorPlaybackSyncSnapshot* out) { if (!h || !out) return 0; const auto s=h->value.snapshot(); out->effective_offset_us=s.compensation.effective_offset_us; out->probe_generation=s.probe_generation; out->last_probe_time_us=s.last_probe_time_us; out->measured_available=s.compensation.measured_available?1:0; out->device_change_pending=s.device_change_pending?1:0; return 1; }
}
