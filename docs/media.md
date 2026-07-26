# Media decoding

DigitorEngine's optional media subsystem is a real FFmpeg demux/software-decode path. CMake uses
pkg-config to discover **libavformat, libavcodec, libavutil, libswscale, and libswresample** as one
imported target. `DIGITOR_REQUIRE_FFMPEG=ON` is recommended for distributions that promise media
support; the API throws a clear build-capability error otherwise.

`open_video_decoder` and `open_audio_decoder` open and probe the container and use FFmpeg's best
stream selection. Decoding uses the send-packet/receive-frame state machine, skips unrelated
packets, sends a null packet at demux EOF, and drains the decoder. A null frame result means clean
EOF. FFmpeg errors (invalid/truncated input, unsupported codec, failed allocation or conversion)
become C++ errors after owned FFmpeg objects are released. The existing C ABI does not expose
these C++ decoder objects, so no exception crosses it.

The deterministic public timebase is microseconds. Video PTS uses best-effort presentation time
(to account for reordering); video duration comes from the decoded frame when supplied. Every
video frame is converted by libswscale to top-down, non-premultiplied RGBA32F and includes source
color primaries, transfer, matrix, and range metadata. Audio is converted by libswresample to
interleaved native float PCM at the decoded stream's sample rate/channel layout, with duration
computed from its output sample count.

`decode(n)` addresses decoded frames in presentation order from the current seek point and returns
null at EOF. Cached frames are stable. Backward indexed reads seek to the beginning and reset the
codec. `seek(pts_us)` performs a backward keyframe seek, flushes decoder buffers, clears packets,
EOF state, and cache, and numbers the first subsequently returned decoded frame zero. Exact seeking
is consequently keyframe-based; callers discard frames until the desired PTS when sample accuracy
is required.

Supported container and codec breadth is the linked FFmpeg build's responsibility. CI exercises
software decoding only; hardware decode, encoding, effects, timeline composition, and UI are not
part of this milestone.
