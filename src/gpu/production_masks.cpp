#include "digitor/production_masks.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace digitor {
namespace {
float clamp01(float v) noexcept { return std::clamp(v, 0.0f, 1.0f); }
std::uint64_t append(std::uint64_t h, const void* p, std::size_t n) noexcept {
  const auto* b = static_cast<const unsigned char*>(p);
  for (std::size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
  return h;
}
bool finite_definition(const MaskDefinition& m) noexcept {
  return std::isfinite(m.transform.translate_x) && std::isfinite(m.transform.translate_y) &&
         std::isfinite(m.transform.scale_x) && std::isfinite(m.transform.scale_y) &&
         std::isfinite(m.transform.rotation_degrees) && m.transform.scale_x != 0.0f &&
         m.transform.scale_y != 0.0f && std::isfinite(m.feather) && m.feather >= 0.0f &&
         std::isfinite(m.expansion) && std::isfinite(m.opacity) && m.opacity >= 0.0f && m.opacity <= 1.0f;
}
MaskPoint inverse_transform(float x, float y, const MaskTransform& t) noexcept {
  x -= t.translate_x; y -= t.translate_y;
  const float a = -t.rotation_degrees * 0.01745329251994329577f;
  const float c = std::cos(a), s = std::sin(a);
  const float rx = c * x - s * y, ry = s * x + c * y;
  return {rx / t.scale_x, ry / t.scale_y};
}
float signed_rectangle(const MaskPoint& p, const std::vector<MaskPoint>& pts) noexcept {
  const MaskPoint lo = pts.size() >= 2 ? pts[0] : MaskPoint{0.25f, 0.25f};
  const MaskPoint hi = pts.size() >= 2 ? pts[1] : MaskPoint{0.75f, 0.75f};
  const float cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f;
  const float hx = std::fabs(hi.x - lo.x) * 0.5f, hy = std::fabs(hi.y - lo.y) * 0.5f;
  const float dx = std::fabs(p.x - cx) - hx, dy = std::fabs(p.y - cy) - hy;
  const float outside = std::hypot(std::max(dx, 0.0f), std::max(dy, 0.0f));
  const float inside = std::min(std::max(dx, dy), 0.0f);
  return outside + inside;
}
float signed_ellipse(const MaskPoint& p, const std::vector<MaskPoint>& pts) noexcept {
  const MaskPoint c = pts.size() >= 2 ? pts[0] : MaskPoint{0.5f, 0.5f};
  const MaskPoint r = pts.size() >= 2 ? pts[1] : MaskPoint{0.25f, 0.25f};
  const float rx = std::max(std::fabs(r.x), 1.0e-6f), ry = std::max(std::fabs(r.y), 1.0e-6f);
  return (std::hypot((p.x - c.x) / rx, (p.y - c.y) / ry) - 1.0f) * std::min(rx, ry);
}
bool polygon_inside(const MaskPoint& p, const std::vector<MaskPoint>& pts) noexcept {
  bool inside = false;
  if (pts.size() < 3) return false;
  for (std::size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
    const auto& a = pts[i]; const auto& b = pts[j];
    const bool cross = ((a.y > p.y) != (b.y > p.y)) &&
      (p.x < (b.x - a.x) * (p.y - a.y) / ((b.y - a.y) == 0.0f ? 1.0e-12f : (b.y - a.y)) + a.x);
    if (cross) inside = !inside;
  }
  return inside;
}
float segment_distance(const MaskPoint& p, const MaskPoint& a, const MaskPoint& b) noexcept {
  const float vx = b.x - a.x, vy = b.y - a.y, wx = p.x - a.x, wy = p.y - a.y;
  const float vv = vx * vx + vy * vy;
  const float t = vv > 0.0f ? clamp01((wx * vx + wy * vy) / vv) : 0.0f;
  return std::hypot(p.x - (a.x + t * vx), p.y - (a.y + t * vy));
}
float signed_polygon(const MaskPoint& p, const std::vector<MaskPoint>& pts) noexcept {
  if (pts.size() < 3) return 1.0f;
  float d = 1.0e9f;
  for (std::size_t i = 0; i < pts.size(); ++i) d = std::min(d, segment_distance(p, pts[i], pts[(i + 1) % pts.size()]));
  return polygon_inside(p, pts) ? -d : d;
}
float evaluate(const MaskDefinition& m, float x, float y) noexcept {
  const MaskPoint p = inverse_transform(x, y, m.transform);
  float sd = 0.0f;
  if (m.shape == MaskShape::ellipse) sd = signed_ellipse(p, m.points);
  else if (m.shape == MaskShape::polygon) sd = signed_polygon(p, m.points);
  else sd = signed_rectangle(p, m.points);
  sd -= m.expansion;
  float a = m.feather > 0.0f ? clamp01(0.5f - sd / (2.0f * m.feather)) : (sd <= 0.0f ? 1.0f : 0.0f);
  if (m.invert) a = 1.0f - a;
  return a * m.opacity;
}
float combine(float base, float value, MaskCombine mode) noexcept {
  if (mode == MaskCombine::add) return std::max(base, value);
  if (mode == MaskCombine::subtract) return base * (1.0f - value);
  if (mode == MaskCombine::intersect) return std::min(base, value);
  return value;
}
}  // namespace

std::uint64_t mask_frame_digest(const MaskFrame& frame) noexcept {
  std::uint64_t h = 1469598103934665603ull;
  h = append(h, &frame.width, sizeof(frame.width)); h = append(h, &frame.height, sizeof(frame.height));
  if (!frame.alpha.empty()) h = append(h, frame.alpha.data(), frame.alpha.size() * sizeof(float));
  return h;
}

MaskResult render_masks_reference(std::uint32_t width, std::uint32_t height,
                                  const std::vector<MaskDefinition>& masks,
                                  MaskFrame& output) {
  MaskResult result;
  if (width == 0u || height == 0u || masks.empty()) return result;
  for (const auto& m : masks) if (!finite_definition(m)) return result;
  output = {width, height, std::vector<float>(static_cast<std::size_t>(width) * height, 0.0f)};
  double sum = 0.0;
  for (std::uint32_t y = 0; y < height; ++y) {
    for (std::uint32_t x = 0; x < width; ++x) {
      const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
      const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
      float a = 0.0f;
      for (const auto& m : masks) a = combine(a, evaluate(m, u, v), m.combine);
      a = clamp01(a); output.alpha[static_cast<std::size_t>(y) * width + x] = a; sum += a;
    }
  }
  result.status = MaskStatus::ready; result.digest = mask_frame_digest(output);
  result.coverage = static_cast<float>(sum / static_cast<double>(output.alpha.size()));
  return result;
}

MaskResult dispatch_masks_gpu(const MaskDispatchPacket& packet, const MaskDispatch& dispatch) {
  MaskResult result;
  if (packet.backend == MaskBackend::cpu || packet.width == 0u || packet.height == 0u ||
      packet.mask_count == 0u || packet.output_handle == 0u || packet.command_handle == 0u ||
      packet.definitions_handle == 0u) return result;
  if (!dispatch) { result.status = MaskStatus::backend_unavailable; return result; }
  result.status = dispatch(packet) ? MaskStatus::ready : MaskStatus::dispatch_failed;
  return result;
}
}  // namespace digitor

extern "C" std::uint32_t digitor_render_masks_f32(
    std::uint32_t width, std::uint32_t height, const DigitorMaskDefinition* masks,
    std::uint32_t mask_count, const DigitorMaskPoint* points, std::uint32_t point_count,
    float* output_alpha, std::uint64_t* digest) {
  if (!width || !height || !masks || !mask_count || !output_alpha || !digest) return 1u;
  std::vector<digitor::MaskDefinition> defs; defs.reserve(mask_count);
  for (std::uint32_t i = 0; i < mask_count; ++i) {
    const auto& c = masks[i];
    if (c.point_offset > point_count || c.point_count > point_count - c.point_offset) return 2u;
    digitor::MaskDefinition m;
    m.shape = static_cast<digitor::MaskShape>(c.shape); m.combine = static_cast<digitor::MaskCombine>(c.combine);
    m.transform = {c.translate_x, c.translate_y, c.scale_x, c.scale_y, c.rotation_degrees};
    m.feather = c.feather; m.expansion = c.expansion; m.opacity = c.opacity; m.invert = c.invert != 0u;
    for (std::uint32_t p = 0; p < c.point_count; ++p) {
      const auto& q = points[c.point_offset + p]; m.points.push_back({q.x, q.y});
    }
    defs.push_back(std::move(m));
  }
  digitor::MaskFrame frame; const auto result = digitor::render_masks_reference(width, height, defs, frame);
  if (result.status != digitor::MaskStatus::ready) return 3u;
  std::copy(frame.alpha.begin(), frame.alpha.end(), output_alpha); *digest = result.digest; return 0u;
}
