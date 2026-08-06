#if defined(_WIN32)

#include "digitor/native_image_codec.hpp"

#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <filesystem>
#include <memory>

namespace digitor {
namespace {
using Microsoft::WRL::ComPtr;

class ComApartment {
 public:
  ComApartment() noexcept : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~ComApartment() { if (SUCCEEDED(result_)) CoUninitialize(); }
  [[nodiscard]] bool ready() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }
 private:
  HRESULT result_;
};

ImageOrientation read_orientation(IWICBitmapFrameDecode* frame) noexcept {
  ComPtr<IWICMetadataQueryReader> reader;
  if (FAILED(frame->GetMetadataQueryReader(&reader))) return ImageOrientation::normal;
  PROPVARIANT value; PropVariantInit(&value);
  const auto hr = reader->GetMetadataByName(L"/app1/ifd/{ushort=274}", &value);
  ImageOrientation orientation = ImageOrientation::normal;
  if (SUCCEEDED(hr)) {
    const auto raw = value.vt == VT_UI2 ? value.uiVal :
                     value.vt == VT_UI4 ? static_cast<USHORT>(value.ulVal) : 1;
    if (raw >= 1 && raw <= 8) orientation = static_cast<ImageOrientation>(raw);
  }
  PropVariantClear(&value);
  return orientation;
}

class WicImageCodec final : public NativeImageCodec {
 public:
  WicImageCodec() noexcept {
    ComApartment apartment;
    if (apartment.ready())
      CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER,
                       IID_PPV_ARGS(&factory_));
  }

  NativeImageCodecCapabilities capabilities() const noexcept override {
    return {true, true, true, true, true, true, true, true, true, false,
            factory_ ? "Windows Imaging Component" : "WIC unavailable"};
  }

  ImageIoResult decode(const NativeImageDecodeRequest& request,
                       NativeStillImageInfo& info,
                       NativeImageBuffer& output) noexcept override {
    if (!factory_) return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "WIC factory unavailable"};
    if (request.progress.cancelled && request.progress.cancelled->load())
      return {DIGITOR_RESULT_RESOURCE_IN_USE, "WIC decode cancelled"};
    try {
      const auto path = std::filesystem::path(request.path).wstring();
      ComPtr<IWICBitmapDecoder> decoder;
      if (FAILED(factory_->CreateDecoderFromFilename(path.c_str(), nullptr,
              GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
        return {DIGITOR_RESULT_INVALID_ARGUMENT, "WIC could not open image"};
      ComPtr<IWICBitmapFrameDecode> frame;
      if (FAILED(decoder->GetFrame(0, &frame)))
        return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC could not read first frame"};
      UINT width{}, height{};
      if (FAILED(frame->GetSize(&width, &height)) || !width || !height)
        return {DIGITOR_RESULT_INVALID_ARGUMENT, "WIC image dimensions are invalid"};
      info.encoded_width = width; info.encoded_height = height;
      info.orientation = read_orientation(frame.Get());
      info.display_width = width; info.display_height = height;
      if (info.orientation == ImageOrientation::rotate_90 ||
          info.orientation == ImageOrientation::rotate_270 ||
          info.orientation == ImageOrientation::mirror_horizontal_rotate_90 ||
          info.orientation == ImageOrientation::mirror_horizontal_rotate_270)
        std::swap(info.display_width, info.display_height);
      if (width > request.limits.max_dimension || height > request.limits.max_dimension)
        return {DIGITOR_RESULT_INVALID_ARGUMENT, "WIC image exceeds configured limits"};

      ComPtr<IWICFormatConverter> converter;
      if (FAILED(factory_->CreateFormatConverter(&converter)) ||
          FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
              WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
        return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "WIC RGBA conversion unavailable"};
      output.width = width; output.height = height;
      output.row_bytes = width * 4U; output.format = NativeImagePixelFormat::rgba8;
      output.premultiplied_alpha = false;
      const auto bytes = static_cast<std::uint64_t>(output.row_bytes) * height;
      if (bytes > request.limits.max_decoded_bytes || bytes > SIZE_MAX)
        return {DIGITOR_RESULT_OUT_OF_MEMORY, "WIC decoded image exceeds memory limit"};
      output.pixels.resize(static_cast<std::size_t>(bytes));
      if (FAILED(converter->CopyPixels(nullptr, output.row_bytes,
              static_cast<UINT>(output.pixels.size()), output.pixels.data())))
        return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC pixel decode failed"};
      info.has_alpha = true;
      info.color_metadata_identity = request.preserve_color_profile ? "wic-embedded-or-srgb" : "srgb";
      if (request.apply_orientation && info.orientation != ImageOrientation::normal) {
        auto oriented = apply_image_orientation(output, info.orientation, request.progress);
        if (!oriented) return oriented;
        info.orientation = ImageOrientation::normal;
      }
      if (request.progress.report) request.progress.report(1.0F);
      return {};
    } catch (const std::bad_alloc&) {
      return {DIGITOR_RESULT_OUT_OF_MEMORY, "WIC decode allocation failed"};
    } catch (...) {
      return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC decode failed"};
    }
  }

  ImageIoResult encode(const NativeImageEncodeRequest& request,
                       const NativeImageBuffer& input) noexcept override {
    if (!factory_) return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "WIC factory unavailable"};
    try {
      NativeImageBuffer prepared = input;
      auto prep = prepare_image_for_export(prepared, request);
      if (!prep) return prep;
      GUID container = GUID_ContainerFormatPng;
      if (request.options.format == ImageExportFormat::jpeg) container = GUID_ContainerFormatJpeg;
      else if (request.options.format == ImageExportFormat::webp) container = GUID_ContainerFormatWebp;
      const auto path = std::filesystem::path(request.path).wstring();
      ComPtr<IWICStream> stream;
      if (FAILED(factory_->CreateStream(&stream)) ||
          FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
        return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC output stream creation failed"};
      ComPtr<IWICBitmapEncoder> encoder;
      if (FAILED(factory_->CreateEncoder(container, nullptr, &encoder)) ||
          FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)))
        return {DIGITOR_RESULT_BACKEND_UNAVAILABLE, "WIC encoder unavailable"};
      ComPtr<IWICBitmapFrameEncode> frame; ComPtr<IPropertyBag2> properties;
      if (FAILED(encoder->CreateNewFrame(&frame, &properties)) ||
          FAILED(frame->Initialize(properties.Get())) ||
          FAILED(frame->SetSize(prepared.width, prepared.height)))
        return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC frame initialization failed"};
      WICPixelFormatGUID format = prepared.format == NativeImagePixelFormat::bgra8
          ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat32bppRGBA;
      if (FAILED(frame->SetPixelFormat(&format)) ||
          FAILED(frame->WritePixels(prepared.height, prepared.row_bytes,
              static_cast<UINT>(prepared.pixels.size()), prepared.pixels.data())) ||
          FAILED(frame->Commit()) || FAILED(encoder->Commit()))
        return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC image encode failed"};
      if (request.progress.report) request.progress.report(1.0F);
      return {};
    } catch (const std::bad_alloc&) {
      return {DIGITOR_RESULT_OUT_OF_MEMORY, "WIC encode allocation failed"};
    } catch (...) {
      return {DIGITOR_RESULT_INTERNAL_ERROR, "WIC encode failed"};
    }
  }
 private:
  ComPtr<IWICImagingFactory2> factory_;
};
}  // namespace

std::unique_ptr<NativeImageCodec> create_windows_wic_image_codec() noexcept {
  try { return std::make_unique<WicImageCodec>(); } catch (...) { return {}; }
}
}  // namespace digitor
#endif
