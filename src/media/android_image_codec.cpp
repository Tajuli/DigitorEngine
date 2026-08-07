#if defined(__ANDROID__)
#include "digitor/native_image_codec.hpp"
#include <android/asset_manager.h>
#include <android/imagedecoder.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
namespace digitor { namespace {

struct AndroidImageDecoderApi final {
  using CreateFromFd = int (*)(int, AImageDecoder**);
  using GetHeaderInfo = const AImageDecoderHeaderInfo* (*)(const AImageDecoder*);
  using GetWidth = int32_t (*)(const AImageDecoderHeaderInfo*);
  using GetHeight = int32_t (*)(const AImageDecoderHeaderInfo*);
  using Delete = void (*)(AImageDecoder*);
  using SetBitmapFormat = int (*)(AImageDecoder*, int32_t);
  using SetUnpremultiplied = int (*)(AImageDecoder*, bool);
  using DecodeImage = int (*)(AImageDecoder*, void*, size_t, size_t);

  void* library{};
  CreateFromFd create_from_fd{};
  GetHeaderInfo get_header_info{};
  GetWidth get_width{};
  GetHeight get_height{};
  Delete destroy{};
  SetBitmapFormat set_bitmap_format{};
  SetUnpremultiplied set_unpremultiplied{};
  DecodeImage decode_image{};

  AndroidImageDecoderApi() noexcept {
    // AImageDecoder is API 30+. Resolve it dynamically so the engine keeps
    // Android API 24 binary compatibility and enables the native codec when
    // the symbols are present on Android 11+ devices.
    library = dlopen("libjnigraphics.so", RTLD_NOW | RTLD_LOCAL);
    if (!library) return;
    create_from_fd = reinterpret_cast<CreateFromFd>(dlsym(library, "AImageDecoder_createFromFd"));
    get_header_info = reinterpret_cast<GetHeaderInfo>(dlsym(library, "AImageDecoder_getHeaderInfo"));
    get_width = reinterpret_cast<GetWidth>(dlsym(library, "AImageDecoderHeaderInfo_getWidth"));
    get_height = reinterpret_cast<GetHeight>(dlsym(library, "AImageDecoderHeaderInfo_getHeight"));
    destroy = reinterpret_cast<Delete>(dlsym(library, "AImageDecoder_delete"));
    set_bitmap_format = reinterpret_cast<SetBitmapFormat>(dlsym(library, "AImageDecoder_setAndroidBitmapFormat"));
    set_unpremultiplied = reinterpret_cast<SetUnpremultiplied>(dlsym(library, "AImageDecoder_setUnpremultipliedRequired"));
    decode_image = reinterpret_cast<DecodeImage>(dlsym(library, "AImageDecoder_decodeImage"));
    if (!available()) {
      dlclose(library);
      library = nullptr;
      create_from_fd = nullptr;
      get_header_info = nullptr;
      get_width = nullptr;
      get_height = nullptr;
      destroy = nullptr;
      set_bitmap_format = nullptr;
      set_unpremultiplied = nullptr;
      decode_image = nullptr;
    }
  }
  ~AndroidImageDecoderApi() { if (library) dlclose(library); }
  AndroidImageDecoderApi(const AndroidImageDecoderApi&) = delete;
  AndroidImageDecoderApi& operator=(const AndroidImageDecoderApi&) = delete;

  [[nodiscard]] bool available() const noexcept {
    return library && create_from_fd && get_header_info && get_width && get_height &&
           destroy && set_bitmap_format && set_unpremultiplied && decode_image;
  }
};

AndroidImageDecoderApi& image_decoder_api() noexcept {
  static AndroidImageDecoderApi api;
  return api;
}

class AndroidImageCodec final : public NativeImageCodec {
 public:
  NativeImageCodecCapabilities capabilities() const noexcept override {
    const bool available=image_decoder_api().available();
    return {available,false,available,false,available,false,available,available,available,false,
            available?"Android AImageDecoder":"Android AImageDecoder unavailable below API 30"};
  }
  ImageIoResult decode(const NativeImageDecodeRequest& request,NativeStillImageInfo& info,
                       NativeImageBuffer& output) noexcept override {
    if(request.progress.cancelled&&request.progress.cancelled->load())
      return {DIGITOR_RESULT_RESOURCE_IN_USE,"Android image decode cancelled"};
    auto& api=image_decoder_api();
    if(!api.available())
      return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,"AImageDecoder requires Android API 30 or newer"};
    const int fd=open(request.path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image file open failed"};
    struct stat st{};if(fstat(fd,&st)!=0||st.st_size<=0){close(fd);return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image file is empty"};}
    AImageDecoder* decoder=nullptr;const auto create=api.create_from_fd(fd,&decoder);close(fd);
    if(create!=ANDROID_IMAGE_DECODER_SUCCESS||!decoder)return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,"AImageDecoder could not open image"};
    const AImageDecoderHeaderInfo* header=api.get_header_info(decoder);
    const int32_t width=api.get_width(header),height=api.get_height(header);
    if(width<=0||height<=0||uint32_t(width)>request.limits.max_dimension||uint32_t(height)>request.limits.max_dimension){api.destroy(decoder);return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image dimensions exceed limits"};}
    api.set_bitmap_format(decoder,ANDROID_BITMAP_FORMAT_RGBA_8888);
    api.set_unpremultiplied(decoder,true);
    output.width=uint32_t(width);output.height=uint32_t(height);output.row_bytes=output.width*4U;
    output.format=NativeImagePixelFormat::rgba8;output.premultiplied_alpha=false;
    const uint64_t bytes=uint64_t(output.row_bytes)*output.height;
    if(bytes>request.limits.max_decoded_bytes){api.destroy(decoder);return {DIGITOR_RESULT_OUT_OF_MEMORY,"Android decoded image exceeds memory limit"};}
    try{output.pixels.resize(size_t(bytes));}catch(...){api.destroy(decoder);return {DIGITOR_RESULT_OUT_OF_MEMORY,"Android image allocation failed"};}
    const auto result=api.decode_image(decoder,output.pixels.data(),output.row_bytes,output.pixels.size());
    api.destroy(decoder);if(result!=ANDROID_IMAGE_DECODER_SUCCESS)return {DIGITOR_RESULT_INTERNAL_ERROR,"AImageDecoder pixel decode failed"};
    info.encoded_width=output.width;info.encoded_height=output.height;info.display_width=output.width;info.display_height=output.height;
    info.orientation=ImageOrientation::normal;info.has_alpha=true;info.color_metadata_identity=request.preserve_color_profile?"android-dataspace-or-srgb":"srgb";
    if(request.progress.report)request.progress.report(1.0F);return {};
  }
  ImageIoResult encode(const NativeImageEncodeRequest&,const NativeImageBuffer&) noexcept override {
    return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,"Android native encode requires the Java ImageEncoder bridge"};
  }
}; }
std::unique_ptr<NativeImageCodec> create_android_image_codec() noexcept {try{return std::make_unique<AndroidImageCodec>();}catch(...){return {};}}
} // namespace digitor
#endif
