#import "DigitorEngineFfiPlugin.h"

#import <CoreVideo/CoreVideo.h>

static NSString *const kDigitorPlatformHostChannel = @"digitor_engine_ffi/platform_host";
static const NSInteger kDigitorCvPixelBufferHandle = 6;
static const NSInteger kDigitorBgra8PixelFormat = 3;
static const NSInteger kDigitorTextureReady = 1;

@interface DigitorEnginePixelBufferTexture : NSObject <FlutterTexture>
@property(nonatomic, assign) CVPixelBufferRef pixelBuffer;
@property(nonatomic, assign) int64_t generation;
@property(nonatomic, assign) uint64_t deviceIdentity;
@property(nonatomic, assign) uint64_t contextIdentity;
@property(nonatomic, assign) size_t width;
@property(nonatomic, assign) size_t height;
- (BOOL)replacePixelBuffer:(CVPixelBufferRef)buffer
                generation:(int64_t)generation
            deviceIdentity:(uint64_t)deviceIdentity
           contextIdentity:(uint64_t)contextIdentity;
@end

@implementation DigitorEnginePixelBufferTexture

- (CVPixelBufferRef)copyPixelBuffer {
  @synchronized(self) {
    return self.pixelBuffer ? CVPixelBufferRetain(self.pixelBuffer) : nil;
  }
}

- (BOOL)replacePixelBuffer:(CVPixelBufferRef)buffer
                generation:(int64_t)generation
            deviceIdentity:(uint64_t)deviceIdentity
           contextIdentity:(uint64_t)contextIdentity {
  if (!buffer || generation <= 0) return NO;
  if (CVPixelBufferGetWidth(buffer) != self.width ||
      CVPixelBufferGetHeight(buffer) != self.height ||
      CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA) {
    return NO;
  }
  @synchronized(self) {
    if (generation <= self.generation) return NO;
    if (self.deviceIdentity != 0 && deviceIdentity != 0 &&
        self.deviceIdentity != deviceIdentity) return NO;
    if (self.contextIdentity != 0 && contextIdentity != 0 &&
        self.contextIdentity != contextIdentity) return NO;
    CVPixelBufferRetain(buffer);
    CVPixelBufferRef old = self.pixelBuffer;
    self.pixelBuffer = buffer;
    self.generation = generation;
    self.deviceIdentity = deviceIdentity;
    self.contextIdentity = contextIdentity;
    if (old) CVPixelBufferRelease(old);
  }
  return YES;
}

- (void)onTextureUnregistered:(NSObject<FlutterTexture> *)texture {
  @synchronized(self) {
    if (self.pixelBuffer) {
      CVPixelBufferRelease(self.pixelBuffer);
      self.pixelBuffer = nil;
    }
  }
}

- (void)dealloc {
  if (_pixelBuffer) CVPixelBufferRelease(_pixelBuffer);
}

@end

@interface DigitorEngineFfiPlugin ()
@property(nonatomic, strong) NSObject<FlutterTextureRegistry> *textures;
@property(nonatomic, strong) NSMutableDictionary<NSNumber *, DigitorEnginePixelBufferTexture *> *hosts;
@end

@implementation DigitorEngineFfiPlugin

+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar> *)registrar {
  FlutterMethodChannel *channel =
      [FlutterMethodChannel methodChannelWithName:kDigitorPlatformHostChannel
                                  binaryMessenger:[registrar messenger]];
  DigitorEngineFfiPlugin *instance = [[DigitorEngineFfiPlugin alloc] init];
  instance.textures = [registrar textures];
  instance.hosts = [NSMutableDictionary dictionary];
  [registrar addMethodCallDelegate:instance channel:channel];
}

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result {
  if ([call.method isEqualToString:@"capabilities"]) {
    result(@{
      @"platform" : @"ios",
      @"supportedHandleTypes" : @[ @(kDigitorCvPixelBufferHandle) ],
      @"directDescriptorPresentation" : @YES,
      @"renderTargetPresentation" : @NO,
    });
    return;
  }

  if ([call.method isEqualToString:@"productionRegistrarToken"]) {
    if (!self.textures) {
      result([FlutterError errorWithCode:@"registrar_unavailable"
                                 message:@"Apple Flutter texture registrar is unavailable."
                                 details:nil]);
      return;
    }
    uintptr_t token = (uintptr_t)(__bridge void *)self.textures;
    result(@((unsigned long long)token));
    return;
  }

  NSDictionary *args = [call.arguments isKindOfClass:[NSDictionary class]] ? call.arguments : nil;
  if (!args) {
    result([FlutterError errorWithCode:@"invalid_arguments"
                               message:@"Expected a map of arguments."
                               details:nil]);
    return;
  }

  if ([call.method isEqualToString:@"createTexture"]) {
    NSInteger handleType = [args[@"handleType"] integerValue];
    NSInteger width = [args[@"width"] integerValue];
    NSInteger height = [args[@"height"] integerValue];
    if (handleType != kDigitorCvPixelBufferHandle || width <= 0 || height <= 0) {
      result([FlutterError errorWithCode:@"unsupported_texture"
                                 message:@"iOS requires a BGRA CVPixelBuffer-backed Metal preview surface."
                                 details:nil]);
      return;
    }
    DigitorEnginePixelBufferTexture *host = [[DigitorEnginePixelBufferTexture alloc] init];
    host.width = (size_t)width;
    host.height = (size_t)height;
    int64_t textureId = [self.textures registerTexture:host];
    if (textureId == 0) {
      result([FlutterError errorWithCode:@"registration_failed"
                                 message:@"Flutter rejected the iOS texture."
                                 details:nil]);
      return;
    }
    self.hosts[@(textureId)] = host;
    result(@{
      @"textureId" : @(textureId),
      @"nativeTargetHandle" : @0,
      @"targetKind" : @"apple-cv-pixel-buffer",
    });
    return;
  }

  NSNumber *textureId = args[@"textureId"];
  if (![textureId isKindOfClass:[NSNumber class]]) {
    result([FlutterError errorWithCode:@"invalid_texture" message:@"textureId is required." details:nil]);
    return;
  }

  if ([call.method isEqualToString:@"disposeTexture"]) {
    [self.textures unregisterTexture:textureId.longLongValue];
    [self.hosts removeObjectForKey:textureId];
    result(nil);
    return;
  }

  DigitorEnginePixelBufferTexture *host = self.hosts[textureId];
  if (!host) {
    result([FlutterError errorWithCode:@"invalid_texture" message:@"Unknown Flutter texture id." details:nil]);
    return;
  }

  if ([call.method isEqualToString:@"present"]) {
    NSInteger handleType = [args[@"handleType"] integerValue];
    NSInteger pixelFormat = [args[@"pixelFormat"] integerValue];
    NSInteger readiness = [args[@"readiness"] integerValue];
    uint64_t nativeHandle = [args[@"nativeHandle"] unsignedLongLongValue];
    int64_t generation = [args[@"generation"] longLongValue];
    BOOL protectedContent = [args[@"protectedContent"] boolValue];
    uint64_t deviceIdentity = [args[@"deviceIdentity"] unsignedLongLongValue];
    uint64_t contextIdentity = [args[@"contextIdentity"] unsignedLongLongValue];
    if (handleType != kDigitorCvPixelBufferHandle ||
        pixelFormat != kDigitorBgra8PixelFormat ||
        readiness != kDigitorTextureReady || nativeHandle == 0 ||
        generation <= 0 || protectedContent) {
      result([FlutterError errorWithCode:@"incompatible_frame"
                                 message:@"iOS Flutter textures require a ready, unprotected BGRA CVPixelBuffer frame."
                                 details:nil]);
      return;
    }
    CVPixelBufferRef buffer = (CVPixelBufferRef)(uintptr_t)nativeHandle;
    if (![host replacePixelBuffer:buffer
                       generation:generation
                   deviceIdentity:deviceIdentity
                  contextIdentity:contextIdentity]) {
      result([FlutterError errorWithCode:@"stale_or_mismatched_frame"
                                 message:@"Frame dimensions, generation, device, context, or pixel format do not match the texture host."
                                 details:nil]);
      return;
    }
    [self.textures textureFrameAvailable:textureId.longLongValue];
    result(nil);
    return;
  }

  if ([call.method isEqualToString:@"markFrameAvailable"]) {
    [self.textures textureFrameAvailable:textureId.longLongValue];
    result(nil);
    return;
  }

  result(FlutterMethodNotImplemented);
}

@end
