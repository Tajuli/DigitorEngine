#pragma once

#include <fcntl.h>
#include <media/NdkMediaExtractor.h>
#include <sys/stat.h>
#include <unistd.h>

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

#define AMediaExtractor_setDataSource(extractor, path) \
  digitor_media_extractor_set_data_source_fd((extractor), (path))
