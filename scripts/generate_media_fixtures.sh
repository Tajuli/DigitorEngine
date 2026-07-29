#!/usr/bin/env sh
# Deterministic, generated-only media corpus. No generated binaries are committed.
set -eu
out=${1:-build/digitor_media_fixtures}
ffmpeg=${FFMPEG_EXECUTABLE:-ffmpeg}
mkdir -p "$out"
common='testsrc2=size=32x24:rate=10:duration=1'
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i "$common" -an -c:v libx264 -pix_fmt yuv420p -threads 1 -metadata creation_time=1970-01-01T00:00:00Z "$out/video.mp4"
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i "$common" -an -c:v mpeg4 -q:v 4 -threads 1 -metadata creation_time=1970-01-01T00:00:00Z "$out/video.mov"
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i "$common" -an -c:v ffv1 -threads 1 "$out/video.mkv"
"$ffmpeg" -hide_banner -loglevel error -y -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=1' -c:a pcm_s16le "$out/audio.wav"
printf 'not a media container\000\377truncated' > "$out/malformed.bin"
