#if defined(__APPLE__)
#include "digitor/native_image_codec.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <memory>
namespace digitor { namespace {
class AppleImageIoCodec final : public NativeImageCodec {
 public:
  NativeImageCodecCapabilities capabilities() const noexcept override {
    return {true,true,true,true,true,true,true,true,true,false,"Apple ImageIO"};
  }
  ImageIoResult decode(const NativeImageDecodeRequest& request, NativeStillImageInfo& info,
                       NativeImageBuffer& output) noexcept override {
    @autoreleasepool {
      if (request.progress.cancelled && request.progress.cancelled->load())
        return {DIGITOR_RESULT_RESOURCE_IN_USE,"ImageIO decode cancelled"};
      CFURLRef url=CFURLCreateFromFileSystemRepresentation(nullptr,
          reinterpret_cast<const UInt8*>(request.path.data()),request.path.size(),false);
      if(!url) return {DIGITOR_RESULT_INVALID_ARGUMENT,"ImageIO path is invalid"};
      CGImageSourceRef source=CGImageSourceCreateWithURL(url,nullptr); CFRelease(url);
      if(!source) return {DIGITOR_RESULT_INVALID_ARGUMENT,"ImageIO could not open image"};
      CGImageRef image=CGImageSourceCreateImageAtIndex(source,0,nullptr);
      if(!image){CFRelease(source);return {DIGITOR_RESULT_INTERNAL_ERROR,"ImageIO decode failed"};}
      const auto width=CGImageGetWidth(image),height=CGImageGetHeight(image);
      if(!width||!height||width>request.limits.max_dimension||height>request.limits.max_dimension){
        CGImageRelease(image);CFRelease(source);return {DIGITOR_RESULT_INVALID_ARGUMENT,"ImageIO dimensions exceed limits"};}
      info.encoded_width=static_cast<uint32_t>(width);info.encoded_height=static_cast<uint32_t>(height);
      info.display_width=info.encoded_width;info.display_height=info.encoded_height;
      info.has_alpha=CGImageGetAlphaInfo(image)!=kCGImageAlphaNone;
      info.color_metadata_identity=request.preserve_color_profile?"imageio-embedded-or-srgb":"srgb";
      output.width=info.encoded_width;output.height=info.encoded_height;output.row_bytes=output.width*4U;
      output.format=NativeImagePixelFormat::rgba8;output.premultiplied_alpha=true;
      const uint64_t bytes=uint64_t(output.row_bytes)*output.height;
      if(bytes>request.limits.max_decoded_bytes){CGImageRelease(image);CFRelease(source);return {DIGITOR_RESULT_OUT_OF_MEMORY,"ImageIO image exceeds memory limit"};}
      try{output.pixels.resize(static_cast<size_t>(bytes));}catch(...){CGImageRelease(image);CFRelease(source);return {DIGITOR_RESULT_OUT_OF_MEMORY,"ImageIO allocation failed"};}
      CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef context=CGBitmapContextCreate(output.pixels.data(),output.width,output.height,8,
          output.row_bytes,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
      CGColorSpaceRelease(color);
      if(!context){CGImageRelease(image);CFRelease(source);return {DIGITOR_RESULT_INTERNAL_ERROR,"ImageIO bitmap context failed"};}
      CGContextDrawImage(context,CGRectMake(0,0,output.width,output.height),image);
      CGContextRelease(context);CGImageRelease(image);CFRelease(source);
      if(request.progress.report)request.progress.report(1.0F);return {};
    }
  }
  ImageIoResult encode(const NativeImageEncodeRequest& request,const NativeImageBuffer& input) noexcept override {
    @autoreleasepool {
      NativeImageBuffer prepared=input;auto prep=prepare_image_for_export(prepared,request);if(!prep)return prep;
      CFStringRef uti=UTTypePNG.identifier;
      if(request.options.format==ImageExportFormat::jpeg)uti=UTTypeJPEG.identifier;
      else if(request.options.format==ImageExportFormat::webp)uti=UTTypeWebP.identifier;
      CFURLRef url=CFURLCreateFromFileSystemRepresentation(nullptr,reinterpret_cast<const UInt8*>(request.path.data()),request.path.size(),false);
      if(!url)return {DIGITOR_RESULT_INVALID_ARGUMENT,"ImageIO output path is invalid"};
      CGImageDestinationRef destination=CGImageDestinationCreateWithURL(url,uti,1,nullptr);CFRelease(url);
      if(!destination)return {DIGITOR_RESULT_BACKEND_UNAVAILABLE,"ImageIO encoder unavailable"};
      CGDataProviderRef provider=CGDataProviderCreateWithData(nullptr,prepared.pixels.data(),prepared.pixels.size(),nullptr);
      CGColorSpaceRef color=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGImageRef image=CGImageCreate(prepared.width,prepared.height,8,32,prepared.row_bytes,color,
          kCGImageAlphaLast|kCGBitmapByteOrder32Big,provider,nullptr,false,kCGRenderingIntentDefault);
      CGColorSpaceRelease(color);CGDataProviderRelease(provider);
      if(!image){CFRelease(destination);return {DIGITOR_RESULT_INTERNAL_ERROR,"ImageIO export image creation failed"};}
      CGImageDestinationAddImage(destination,image,nullptr);const bool ok=CGImageDestinationFinalize(destination);
      CGImageRelease(image);CFRelease(destination);
      if(!ok)return {DIGITOR_RESULT_INTERNAL_ERROR,"ImageIO encode failed"};
      if(request.progress.report)request.progress.report(1.0F);return {};
    }
  }
}; }
std::unique_ptr<NativeImageCodec> create_apple_imageio_codec() noexcept {try{return std::make_unique<AppleImageIoCodec>();}catch(...){return {};}}
} // namespace digitor
#endif
