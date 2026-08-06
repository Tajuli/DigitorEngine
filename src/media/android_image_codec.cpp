#if defined(__ANDROID__)
#include "digitor/native_image_codec.hpp"
#include <android/asset_manager.h>
#include <android/imagedecoder.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>
namespace digitor { namespace {
class AndroidImageCodec final : public NativeImageCodec {
 public:
  NativeImageCodecCapabilities capabilities() const noexcept override {
    return {true,false,true,false,true,false,true,true,true,false,"Android AImageDecoder"};
  }
  ImageIoResult decode(const NativeImageDecodeRequest& request,NativeStillImageInfo& info,
                       NativeImageBuffer& output) noexcept override {
    if(request.progress.cancelled&&request.progress.cancelled->load())
      return {DIGITOR_RESULT_RESOURCE_IN_USE,"Android image decode cancelled"};
    const int fd=open(request.path.c_str(),O_RDONLY|O_CLOEXEC);if(fd<0)return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image file open failed"};
    struct stat st{};if(fstat(fd,&st)!=0||st.st_size<=0){close(fd);return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image file is empty"};}
    AImageDecoder* decoder=nullptr;const auto create=AImageDecoder_createFromFd(fd,&decoder);close(fd);
    if(create!=ANDROID_IMAGE_DECODER_SUCCESS||!decoder)return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,"AImageDecoder could not open image"};
    const AImageDecoderHeaderInfo* header=AImageDecoder_getHeaderInfo(decoder);
    const int32_t width=AImageDecoderHeaderInfo_getWidth(header),height=AImageDecoderHeaderInfo_getHeight(header);
    if(width<=0||height<=0||uint32_t(width)>request.limits.max_dimension||uint32_t(height)>request.limits.max_dimension){AImageDecoder_delete(decoder);return {DIGITOR_RESULT_INVALID_ARGUMENT,"Android image dimensions exceed limits"};}
    AImageDecoder_setAndroidBitmapFormat(decoder,ANDROID_BITMAP_FORMAT_RGBA_8888);
    AImageDecoder_setUnpremultipliedRequired(decoder,true);
    output.width=uint32_t(width);output.height=uint32_t(height);output.row_bytes=output.width*4U;
    output.format=NativeImagePixelFormat::rgba8;output.premultiplied_alpha=false;
    const uint64_t bytes=uint64_t(output.row_bytes)*output.height;
    if(bytes>request.limits.max_decoded_bytes){AImageDecoder_delete(decoder);return {DIGITOR_RESULT_OUT_OF_MEMORY,"Android decoded image exceeds memory limit"};}
    try{output.pixels.resize(size_t(bytes));}catch(...){AImageDecoder_delete(decoder);return {DIGITOR_RESULT_OUT_OF_MEMORY,"Android image allocation failed"};}
    const auto result=AImageDecoder_decodeImage(decoder,output.pixels.data(),output.row_bytes,output.pixels.size());
    AImageDecoder_delete(decoder);if(result!=ANDROID_IMAGE_DECODER_SUCCESS)return {DIGITOR_RESULT_INTERNAL_ERROR,"AImageDecoder pixel decode failed"};
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
