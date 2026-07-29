# Media decoding

DigitorEngine's optional media subsystem is a real FFmpeg demux/software-decode path. CMake uses
pkg-config or `DIGITOR_FFMPEG_ROOT` to discover **libavformat, libavcodec, libavutil, libswscale,
and libswresample** as one imported target. The concrete library result is available as
`DIGITOR_FFMPEG_LIBRARIES`. `DIGITOR_REQUIRE_FFMPEG=ON` is recommended for distributions that
promise media support; the API throws a clear build-capability error otherwise.

The engine never depends on the `ffmpeg` command-line executable. CMake discovers it separately
as the optional `DIGITOR_FFMPEG_CLI`. `DIGITOR_GENERATE_TEST_MEDIA` defaults to `OFF`; enabling it
requires the CLI and generates MP4/MOV/MKV/WAV test containers. With generation disabled, media
tests use the repository-owned deterministic Y4M/WAV source fixtures and malformed fixture.

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

`decode(n)` decodes through the requested frame in presentation order and retains recent results
in an LRU cache. An older evicted frame requires a seek, and null is returned stably after EOF.
Timestamp random access is provided by `seek(pts_us)`, which performs a backward
keyframe seek, flushes decoder buffers, clears packets,
EOF state, and cache, discards video preroll, and numbers the first subsequently returned decoded
frame zero.

Supported container and codec breadth is the linked FFmpeg build's responsibility. CI exercises
software decoding only; hardware decode, encoding, effects, timeline composition, and UI are not
part of this milestone.
