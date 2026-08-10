import 'package:flutter/widgets.dart';

import 'editor_controller.dart';

/// Ready-to-use Flutter viewport for [DigitorEditorController].
///
/// The widget never receives a native GPU handle. It rebuilds only from the
/// Flutter texture id published by the controller after DigitorEngine has
/// completed native presentation and ownership synchronization.
final class DigitorPreviewTexture extends StatelessWidget {
  const DigitorPreviewTexture({
    required this.controller,
    this.fit = BoxFit.contain,
    this.placeholder = const SizedBox.shrink(),
    super.key,
  });

  final DigitorEditorController controller;
  final BoxFit fit;
  final Widget placeholder;

  @override
  Widget build(BuildContext context) {
    return ListenableBuilder(
      listenable: controller,
      builder: (context, child) {
        final state = controller.state;
        final textureId = state.textureId;
        if (textureId == null ||
            state.previewWidth <= 0 ||
            state.previewHeight <= 0) {
          return child!;
        }
        return FittedBox(
          fit: fit,
          child: SizedBox(
            width: state.previewWidth.toDouble(),
            height: state.previewHeight.toDouble(),
            child: Texture(textureId: textureId),
          ),
        );
      },
      child: placeholder,
    );
  }
}
