#include "digitor/media.hpp"

#include <memory>
#include <string>

namespace digitor {

// The production timeline contract target intentionally has no real-media
// runtime dependency. timeline_audio_session.cpp and the canonical
// ProductionAudioMediaPipeline are compiled into this target so their ABI and
// integration remain covered, while real decoder discovery remains owned by
// the full DigitorEngine runtime/qualification targets.
std::unique_ptr<AudioDecoder> open_audio_decoder(const std::string&, DecoderOptions) {
    return {};
}

}  // namespace digitor
