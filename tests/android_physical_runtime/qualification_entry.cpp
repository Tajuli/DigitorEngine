#include "media_extractor_fd_compat.hpp"

int digitor_android_physical_runtime_main(int argc, char** argv);

int main(int argc, char** argv) {
  const int result = digitor_android_physical_runtime_main(argc, argv);
  digitor_release_deferred_image_readers();
  return result;
}
