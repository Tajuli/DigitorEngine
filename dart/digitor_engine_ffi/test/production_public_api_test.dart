import 'package:digitor_engine_ffi/digitor_engine_ffi.dart';
import 'package:flutter_test/flutter_test.dart';

void main() {
  test(
    'production exceptions expose symbolic native result and diagnostic',
    () {
      const error = DigitorProductionException(
        'preview',
        100,
        'decoded timestamps moved backwards without a seek',
      );

      expect(error.symbolicCode, 'DIGITOR_RESULT_INTERNAL_ERROR');
      expect(error.toString(), contains('operation: preview'));
      expect(error.toString(), contains('result: 100'));
      expect(error.toString(), contains('DIGITOR_RESULT_INTERNAL_ERROR'));
      expect(error.toString(), contains('decoded timestamps moved backwards'));
    },
  );

  test('production pixel formats match the native ABI', () {
    expect(DigitorPixelFormat.rgba32Float.nativeValue, 1);
    expect(DigitorPixelFormat.rgba8Unorm.nativeValue, 2);
    expect(DigitorPixelFormat.bgra8Unorm.nativeValue, 3);
    expect(DigitorPixelFormat.rgba16Float.nativeValue, 4);
  });

  test('native texture readiness matches the native ABI', () {
    expect(DigitorNativeTextureReadiness.notReady.nativeValue, 0);
    expect(DigitorNativeTextureReadiness.ready.nativeValue, 1);
    expect(DigitorNativeTextureReadiness.deviceLost.nativeValue, 2);
  });

  test('node effect order matches the production C ABI', () {
    expect(DigitorNodeEffectType.blur.index, 0);
    expect(DigitorNodeEffectType.sharpen.index, 1);
    expect(DigitorNodeEffectType.glow.index, 2);
    expect(DigitorNodeEffectType.lensDistortion.index, 3);
    expect(DigitorNodeEffectType.noise.index, 4);
    expect(DigitorNodeEffectType.filmGrain.index, 5);
    expect(DigitorNodeEffectType.chromaticAberration.index, 6);
    expect(DigitorNodeEffectType.vignette.index, 7);
    expect(DigitorNodeEffectType.motionBlur.index, 8);
  });

  test('power window and LUT enum order match the production C ABI', () {
    expect(DigitorPowerWindowShape.rectangle.index, 0);
    expect(DigitorPowerWindowShape.ellipse.index, 1);
    expect(DigitorPowerWindowShape.linearGradient.index, 2);
    expect(DigitorLutInterpolation.nearest.index, 0);
    expect(DigitorLutInterpolation.linear.index, 1);
    expect(DigitorLutInterpolation.tetrahedral.index, 2);
  });

  test(
    'production controls are constructible without a native library call',
    () {
      const correction = DigitorCorrection(
        exposure: 0.1,
        contrast: 0.2,
        temperature: -0.1,
        colorBoost: 0.3,
      );
      expect(correction.exposure, 0.1);
      expect(correction.contrast, 0.2);
      expect(correction.temperature, -0.1);
      expect(correction.colorBoost, 0.3);

      const effect = DigitorNodeEffect(
        type: DigitorNodeEffectType.vignette,
        amount: 0.25,
      );
      expect(effect.type, DigitorNodeEffectType.vignette);
      expect(effect.amount, 0.25);
    },
  );
}
