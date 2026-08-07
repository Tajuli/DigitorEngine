#pragma once

#include <cstdlib>
#include <fcntl.h>
#include <media/NdkImageReader.h>
#include <media/NdkMediaExtractor.h>
#include <sys/stat.h>
#include <unistd.h>

#include <vector>

inline media_status_t digitor_media_extractor_set_data_source_fd(
    AMediaExtractor* extractor,
    const char* path) {
  if (!extractor || !path) return AMEDIA_ERROR_INVALID_PARAMETER;

  const int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) return AMEDIA_ERROR_IO;

  struct stat st {};
  if (fstat(fd, &st) != 0 || st.st_size <= 0) {
    close(fd);
    return AMEDIA_ERROR_IO;
  }

  const auto status = AMediaExtractor_setDataSourceFd(
      extractor, fd, 0, static_cast<off64_t>(st.st_size));

  // NDK extractor duplicates/owns what it needs after setDataSourceFd returns.
  close(fd);
  return status;
}

// AImageReader_delete() returns every acquired AImage to the system. The
// qualification harness intentionally carries one acquired AImage/AHB out of
// decode_one_frame() so Vulkan can import it afterwards. Defer reader deletion
// until process shutdown, after the DecodedFrame's AImage has been destroyed.
// This preserves the Android NDK lifetime contract without changing production
// engine code or weakening the qualification criteria.
inline std::vector<AImageReader*>& digitor_deferred_image_readers() {
  static auto* readers = new std::vector<AImageReader*>();
  return *readers;
}

inline void digitor_release_deferred_image_readers() {
  auto& readers = digitor_deferred_image_readers();
  for (auto* reader : readers) {
    if (reader) AImageReader_delete(reader);
  }
  readers.clear();
}

inline void digitor_defer_image_reader_delete(AImageReader* reader) {
  if (!reader) return;
  auto& readers = digitor_deferred_image_readers();
  if (readers.empty()) std::atexit(digitor_release_deferred_image_readers);
  readers.push_back(reader);
}

#define AMediaExtractor_setDataSource(extractor, path) \
  digitor_media_extractor_set_data_source_fd((extractor), (path))
#define AImageReader_delete(reader) digitor_defer_image_reader_delete((reader))
