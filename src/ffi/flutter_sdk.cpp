#include "digitor/digitor.h"
#include "digitor/renderer.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <vector>

struct DigitorSdkSession {
  std::mutex lifecycle_mutex;
  std::mutex mutex;
  std::thread worker;
  std::atomic_bool busy{false};
  std::atomic_bool cancel{false};
  bool destroying{false};
  int64_t position{};
  DigitorColorControls color{0, 1, 1};
  std::vector<uint8_t> texture;
  uint32_t width{}, height{};
  uint64_t generation{};
  digitor::SharedRenderer renderer;

  DigitorSdkSession()
      : renderer([this](auto &graph, const auto &request, auto &out) {
          if (request.width > std::numeric_limits<std::size_t>::max() /
                                  request.height)
            throw std::overflow_error("preview dimensions are too large");
          auto target = graph.create_transient(
              uint64_t(request.width) * request.height * sizeof(digitor::Color));
          graph.add_pass({"flutter-preview", {},
                          {{target, digitor::ResourceState::shader_write}},
                          [&, this](auto &encoder) {
                            encoder.dispatch([&, this] {
                              out.pixels.resize(size_t(request.width) *
                                                request.height);
                              DigitorColorControls controls;
                              {
                                std::scoped_lock lock(mutex);
                                controls = color;
                              }
                              for (uint32_t y = 0; y < request.height; ++y) {
                                for (uint32_t x = 0; x < request.width; ++x) {
                                  const float red = request.width > 1
                                                        ? float(x) / float(request.width - 1)
                                                        : 0.0f;
                                  const float green = request.height > 1
                                                          ? float(y) / float(request.height - 1)
                                                          : 0.0f;
                                  const auto grade = [&](float value) {
                                    value = (value - .5f) * controls.contrast + .5f +
                                            controls.exposure * .1f;
                                    const float luma = (red + green + .5f) / 3.0f;
                                    return std::clamp(luma + (value - luma) *
                                                               controls.saturation,
                                                      0.0f, 1.0f);
                                  };
                                  out.pixels[size_t(y) * request.width + x] =
                                      {grade(red), grade(green), grade(.5f), 1};
                                }
                              }
                            });
                          }});
        }) {}
};

namespace {
std::mutex sessions_mutex;
std::unordered_map<DigitorSdkSession *, std::shared_ptr<DigitorSdkSession>> sessions;

std::shared_ptr<DigitorSdkSession> acquire_session(DigitorSdkSession *session) {
  std::scoped_lock lock(sessions_mutex);
  if (!session) return {};
  const auto found = sessions.find(session);
  return found == sessions.end() ? std::shared_ptr<DigitorSdkSession>{}
                                 : found->second;
}

void join_old(DigitorSdkSession *session) {
  if (session->worker.joinable()) session->worker.join();
}

template <class Function>
DigitorResult sdk_guard(Function &&function) noexcept {
  try {
    return function();
  } catch (const std::bad_alloc &) {
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
}

uint8_t to_byte(float value) {
  return uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + .5f);
}

void invoke_completion(DigitorAsyncCallback callback, DigitorResult result,
                       void *user_data) noexcept {
  if (!callback) return;
  try {
    callback(result, user_data);
  } catch (...) {
    // No exception may cross a C callback boundary or escape a worker thread.
  }
}
} // namespace

extern "C" {

DigitorResult digitor_sdk_create(DigitorSdkSession **out_session) {
  if (!out_session) return DIGITOR_RESULT_INVALID_ARGUMENT;
  *out_session = nullptr;
  return sdk_guard([&] {
    auto owned = std::make_shared<DigitorSdkSession>();
    auto *session = owned.get();
    {
      std::scoped_lock lock(sessions_mutex);
      sessions.emplace(session, std::move(owned));
    }
    *out_session = session;
    return DIGITOR_RESULT_OK;
  });
}

DigitorResult digitor_sdk_destroy(DigitorSdkSession *session) {
  return sdk_guard([&] {
    std::shared_ptr<DigitorSdkSession> owned;
    {
      std::scoped_lock lock(sessions_mutex);
      const auto found = sessions.find(session);
      if (!session || found == sessions.end()) return DIGITOR_RESULT_INVALID_ARGUMENT;
      owned = found->second;
      std::scoped_lock lifecycle(owned->lifecycle_mutex);
      if (session->worker.joinable() &&
          session->worker.get_id() == std::this_thread::get_id())
        return DIGITOR_RESULT_RESOURCE_IN_USE;
      owned->destroying = true;
      sessions.erase(found);
    }
    owned->cancel.store(true);
    join_old(owned.get());
    return DIGITOR_RESULT_OK;
  });
}

DigitorResult digitor_sdk_set_color(DigitorSdkSession *session,
                                    DigitorColorControls controls) {
  auto owned = acquire_session(session);
  if (!owned || !std::isfinite(controls.exposure) ||
      !std::isfinite(controls.contrast) ||
      !std::isfinite(controls.saturation))
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lock(session->mutex);
  session->color = controls;
  return DIGITOR_RESULT_OK;
}

DigitorResult digitor_sdk_preview_async(DigitorSdkSession *session, int64_t frame,
                                        uint32_t width, uint32_t height,
                                        DigitorAsyncCallback callback,
                                        void *user_data) {
  auto owned = acquire_session(session);
  if (!owned || !width || !height || frame < 0 ||
      width > std::numeric_limits<std::size_t>::max() / height / 4u)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (session->busy.exchange(true)) return DIGITOR_RESULT_RESOURCE_IN_USE;
  join_old(session);
  session->cancel.store(false);
  try {
    session->worker = std::thread([=, owned = std::move(owned)] {
      DigitorResult result = DIGITOR_RESULT_OK;
      try {
        auto rendered = session->renderer.render({frame, width, height, {}});
        if (!session->cancel.load()) {
          std::vector<uint8_t> pixels(rendered.pixels.size() * 4);
          for (size_t i = 0; i < rendered.pixels.size(); ++i) {
            pixels[i * 4] = to_byte(rendered.pixels[i].r);
            pixels[i * 4 + 1] = to_byte(rendered.pixels[i].g);
            pixels[i * 4 + 2] = to_byte(rendered.pixels[i].b);
            pixels[i * 4 + 3] = to_byte(rendered.pixels[i].a);
          }
          std::scoped_lock lock(session->mutex);
          session->texture = std::move(pixels);
          session->width = width;
          session->height = height;
          session->position = frame;
          ++session->generation;
        }
      } catch (const std::bad_alloc &) {
        result = DIGITOR_RESULT_OUT_OF_MEMORY;
      } catch (...) {
        result = DIGITOR_RESULT_INTERNAL_ERROR;
      }
      invoke_completion(callback, result, user_data);
      session->busy.store(false);
    });
  } catch (const std::bad_alloc &) {
    session->busy.store(false);
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    session->busy.store(false);
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult digitor_sdk_seek_async(DigitorSdkSession *session, int64_t frame,
                                     DigitorAsyncCallback callback,
                                     void *user_data) {
  auto owned = acquire_session(session);
  if (!owned || frame < 0) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (session->busy.exchange(true)) return DIGITOR_RESULT_RESOURCE_IN_USE;
  join_old(session);
  session->cancel.store(false);
  try {
    session->worker = std::thread([=, owned = std::move(owned)] {
      {
        std::scoped_lock lock(session->mutex);
        session->position = frame;
      }
      invoke_completion(callback, DIGITOR_RESULT_OK, user_data);
      session->busy.store(false);
    });
  } catch (const std::bad_alloc &) {
    session->busy.store(false);
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    session->busy.store(false);
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult digitor_sdk_get_native_texture(DigitorSdkSession *session,
                                             DigitorNativeTexture *out_texture) {
  if (!out_texture) return DIGITOR_RESULT_INVALID_ARGUMENT;
  *out_texture = {};
  auto owned = acquire_session(session);
  if (!owned) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lock(session->mutex);
  if (session->texture.empty()) return DIGITOR_RESULT_NOT_INITIALIZED;
  out_texture->pixels = session->texture.data();
  out_texture->width = session->width;
  out_texture->height = session->height;
  out_texture->row_bytes = session->width * 4;
  out_texture->generation = session->generation;
  return DIGITOR_RESULT_OK;
}

DigitorResult digitor_sdk_export_async(
    DigitorSdkSession *session, const char *path, int32_t format, int32_t codec,
    int64_t first, int64_t last, uint32_t width, uint32_t height,
    DigitorExportProgressCallback progress, DigitorAsyncCallback completion,
    void *user_data) {
  auto owned = acquire_session(session);
  if (!owned || !path || !*path || first < 0 || last < first ||
      !width || !height || format < 0 || format > 5 || codec < 0 || codec > 2 ||
      width > std::numeric_limits<std::size_t>::max() / height / 4u)
    return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  if (session->busy.exchange(true)) return DIGITOR_RESULT_RESOURCE_IN_USE;
  join_old(session);
  session->cancel.store(false);
  std::string output;
  try {
    output = path;
    session->worker = std::thread([=, owned = std::move(owned)] {
      DigitorResult result = DIGITOR_RESULT_OK;
      try {
        digitor::ExportSettings settings;
        settings.format = static_cast<digitor::ExportFormat>(format);
        settings.video_codec = static_cast<digitor::VideoCodec>(codec);
        settings.first = first;
        settings.last = last;
        settings.width = width;
        settings.height = height;
        settings.cancel = std::shared_ptr<std::atomic_bool>(
            &session->cancel, [](auto *) {});
        settings.progress = [=](const digitor::ExportProgress &value) {
          if (progress)
            progress(value.fraction, value.completed, value.total, user_data);
        };
        digitor::ExportRenderer(session->renderer).export_to(output, settings);
      } catch (const std::bad_alloc &) {
        result = DIGITOR_RESULT_OUT_OF_MEMORY;
      } catch (...) {
        result = DIGITOR_RESULT_INTERNAL_ERROR;
      }
      invoke_completion(completion, result, user_data);
      session->busy.store(false);
    });
  } catch (const std::bad_alloc &) {
    session->busy.store(false);
    return DIGITOR_RESULT_OUT_OF_MEMORY;
  } catch (...) {
    session->busy.store(false);
    return DIGITOR_RESULT_INTERNAL_ERROR;
  }
  return DIGITOR_RESULT_OK;
}

DigitorResult digitor_sdk_cancel(DigitorSdkSession *session) {
  auto owned = acquire_session(session);
  if (!owned) return DIGITOR_RESULT_INVALID_ARGUMENT;
  std::scoped_lock lifecycle(owned->lifecycle_mutex);
  if (owned->destroying) return DIGITOR_RESULT_INVALID_ARGUMENT;
  session->cancel.store(true);
  return DIGITOR_RESULT_OK;
}

} // extern "C"
