package com.primedigitor.digitor_engine_ffi

import android.media.MediaPlayer
import android.view.Surface
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry

class DigitorEngineFfiPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    companion object {
        private const val CHANNEL = "digitor_engine_ffi/platform_host"
        private val SUPPORTED_HANDLE_TYPES = listOf(4, 7, 8, 9)

        init {
            System.loadLibrary("digitor_engine_ffi_platform_host")
        }
    }

    private data class HostTexture(
        val producer: TextureRegistry.SurfaceProducer,
        val handleType: Int,
        var nativeWindow: Long = 0L,
        var generation: Long = 0L,
    )

    private data class SourcePreview(
        val producer: TextureRegistry.SurfaceProducer,
        val player: MediaPlayer,
    )

    private lateinit var channel: MethodChannel
    private lateinit var textures: TextureRegistry
    private val hosts = mutableMapOf<Long, HostTexture>()
    private val sourcePreviews = mutableMapOf<Long, SourcePreview>()

    private external fun nativeAcquireWindow(surface: Surface): Long

    private external fun nativeReleaseWindow(handle: Long)

    private external fun nativeProductionRegistrarToken(): Long

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        textures = binding.textureRegistry
        channel = MethodChannel(binding.binaryMessenger, CHANNEL)
        channel.setMethodCallHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        sourcePreviews.keys.toList().forEach(::disposeSourcePreview)
        hosts.keys.toList().forEach(::disposeTexture)
    }

    override fun onMethodCall(call: MethodCall, result: MethodChannel.Result) {
        when (call.method) {
            "capabilities" -> result.success(
                mapOf(
                    "platform" to "android",
                    "supportedHandleTypes" to SUPPORTED_HANDLE_TYPES,
                    "directDescriptorPresentation" to false,
                    "renderTargetPresentation" to true,
                ),
            )
            "productionRegistrarToken" -> {
                val token = nativeProductionRegistrarToken()
                if (token == 0L) {
                    result.error(
                        "registrar_unavailable",
                        "Android production registrar token is unavailable.",
                        null,
                    )
                } else {
                    result.success(token)
                }
            }
            "createTexture" -> createTexture(call, result)
            "refreshTextureTarget" -> refreshTextureTarget(call, result)
            "present" -> result.error(
                "render_target_required",
                "Android uses a Flutter SurfaceProducer render target. Render the selected Vulkan/GLES frame into nativeTargetHandle and call markFrameAvailable.",
                null,
            )
            "markFrameAvailable" -> markFrameAvailable(call, result)
            "disposeTexture" -> {
                val textureId = call.argument<Number>("textureId")?.toLong()
                if (textureId == null) {
                    result.error("invalid_texture", "textureId is required.", null)
                } else {
                    disposeTexture(textureId)
                    result.success(null)
                }
            }
            "createSourcePreview" -> createSourcePreview(call, result)
            "sourcePreviewPlay" -> sourcePreviewPlay(call, result)
            "sourcePreviewPause" -> sourcePreviewPause(call, result)
            "sourcePreviewSeek" -> sourcePreviewSeek(call, result)
            "disposeSourcePreview" -> {
                val textureId = call.argument<Number>("textureId")?.toLong()
                if (textureId == null) {
                    result.error("invalid_texture", "textureId is required.", null)
                } else {
                    disposeSourcePreview(textureId)
                    result.success(null)
                }
            }
            else -> result.notImplemented()
        }
    }

    private fun createTexture(call: MethodCall, result: MethodChannel.Result) {
        val handleType = call.argument<Number>("handleType")?.toInt()
        val width = call.argument<Number>("width")?.toInt()
        val height = call.argument<Number>("height")?.toInt()
        if (handleType == null || handleType !in SUPPORTED_HANDLE_TYPES ||
            width == null || height == null || width <= 0 || height <= 0
        ) {
            result.error(
                "unsupported_texture",
                "Android requires a Vulkan/AHardwareBuffer/EGL/GL GPU path and positive dimensions.",
                null,
            )
            return
        }

        val producer = textures.createSurfaceProducer()
        producer.setSize(width, height)
        val host = HostTexture(producer, handleType)
        val textureId = producer.id()
        hosts[textureId] = host
        producer.setCallback(object : TextureRegistry.SurfaceProducer.Callback {
            override fun onSurfaceAvailable() {
                refreshNativeWindow(textureId, notifyDart = true)
            }

            override fun onSurfaceCleanup() {
                releaseNativeWindow(host)
                notifyTargetChanged(textureId, 0L, false)
            }
        })
        refreshNativeWindow(textureId, notifyDart = false)

        result.success(
            mapOf(
                "textureId" to textureId,
                "nativeTargetHandle" to host.nativeWindow,
                "targetKind" to "android-native-window",
            ),
        )
    }

    private fun refreshTextureTarget(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        val host = textureId?.let(hosts::get)
        if (textureId == null || host == null) {
            result.error("invalid_texture", "Unknown Flutter texture id.", null)
            return
        }
        refreshNativeWindow(textureId, notifyDart = false)
        result.success(
            mapOf(
                "textureId" to textureId,
                "nativeTargetHandle" to host.nativeWindow,
                "targetKind" to "android-native-window",
            ),
        )
    }

    private fun markFrameAvailable(call: MethodCall, result: MethodChannel.Result) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        val generation = call.argument<Number>("generation")?.toLong()
        val host = textureId?.let(hosts::get)
        if (textureId == null || generation == null || generation <= 0 || host == null) {
            result.error("invalid_texture", "Valid textureId and generation are required.", null)
            return
        }
        if (host.nativeWindow == 0L) {
            result.error("surface_unavailable", "Flutter SurfaceProducer has no live Surface.", null)
            return
        }
        if (generation <= host.generation) {
            result.error("stale_generation", "Preview generations must increase.", null)
            return
        }
        host.generation = generation
        host.producer.scheduleFrame()
        result.success(null)
    }

    private fun createSourcePreview(call: MethodCall, result: MethodChannel.Result) {
        val path = call.argument<String>("path")
        if (path.isNullOrBlank()) {
            result.error("invalid_media", "A local media path is required.", null)
            return
        }

        val producer = textures.createSurfaceProducer()
        producer.setSize(1280, 720)
        val textureId = producer.id()
        val player = MediaPlayer()
        var completed = false

        fun fail(code: String, message: String) {
            if (completed) return
            completed = true
            sourcePreviews.remove(textureId)
            producer.setCallback(null)
            runCatching { player.reset() }
            runCatching { player.release() }
            producer.release()
            result.error(code, message, null)
        }

        producer.setCallback(object : TextureRegistry.SurfaceProducer.Callback {
            override fun onSurfaceAvailable() {
                runCatching { player.setSurface(producer.surface) }
            }

            override fun onSurfaceCleanup() {
                runCatching { player.setSurface(null) }
            }
        })

        player.setOnVideoSizeChangedListener { _, width, height ->
            if (width > 0 && height > 0) {
                producer.setSize(width, height)
            }
        }
        player.setOnPreparedListener { prepared ->
            if (completed) return@setOnPreparedListener
            val width = prepared.videoWidth.coerceAtLeast(1)
            val height = prepared.videoHeight.coerceAtLeast(1)
            producer.setSize(width, height)
            sourcePreviews[textureId] = SourcePreview(producer, prepared)
            completed = true
            result.success(
                mapOf(
                    "textureId" to textureId,
                    "width" to width,
                    "height" to height,
                    "durationMs" to prepared.duration.coerceAtLeast(0),
                ),
            )
        }
        player.setOnErrorListener { _, what, extra ->
            fail(
                "preview_player_error",
                "Android source preview failed (what=$what, extra=$extra).",
            )
            true
        }

        try {
            player.setDataSource(path)
            player.setSurface(producer.surface)
            player.prepareAsync()
        } catch (error: Throwable) {
            fail(
                "preview_open_failed",
                error.message ?: "Android source preview could not open the media file.",
            )
        }
    }

    private fun sourcePreviewPlay(call: MethodCall, result: MethodChannel.Result) {
        withSourcePreview(call, result) { preview ->
            preview.player.start()
        }
    }

    private fun sourcePreviewPause(call: MethodCall, result: MethodChannel.Result) {
        withSourcePreview(call, result) { preview ->
            if (preview.player.isPlaying) preview.player.pause()
        }
    }

    private fun sourcePreviewSeek(call: MethodCall, result: MethodChannel.Result) {
        val positionMs = call.argument<Number>("positionMs")?.toLong()
        if (positionMs == null || positionMs < 0) {
            result.error("invalid_position", "positionMs must be non-negative.", null)
            return
        }
        withSourcePreview(call, result) { preview ->
            val bounded = positionMs.coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
            preview.player.seekTo(bounded)
        }
    }

    private inline fun withSourcePreview(
        call: MethodCall,
        result: MethodChannel.Result,
        action: (SourcePreview) -> Unit,
    ) {
        val textureId = call.argument<Number>("textureId")?.toLong()
        val preview = textureId?.let(sourcePreviews::get)
        if (textureId == null || preview == null) {
            result.error("invalid_texture", "Unknown source preview texture id.", null)
            return
        }
        try {
            action(preview)
            result.success(null)
        } catch (error: Throwable) {
            result.error(
                "preview_player_error",
                error.message ?: "Android source preview operation failed.",
                null,
            )
        }
    }

    private fun refreshNativeWindow(textureId: Long, notifyDart: Boolean) {
        val host = hosts[textureId] ?: return
        val surface = host.producer.surface
        val next = nativeAcquireWindow(surface)
        if (next == 0L) return
        releaseNativeWindow(host)
        host.nativeWindow = next
        if (notifyDart) notifyTargetChanged(textureId, next, true)
    }

    private fun releaseNativeWindow(host: HostTexture) {
        if (host.nativeWindow != 0L) {
            nativeReleaseWindow(host.nativeWindow)
            host.nativeWindow = 0L
        }
    }

    private fun disposeTexture(textureId: Long) {
        val host = hosts.remove(textureId) ?: return
        host.producer.setCallback(null)
        releaseNativeWindow(host)
        host.producer.release()
    }

    private fun disposeSourcePreview(textureId: Long) {
        val preview = sourcePreviews.remove(textureId) ?: return
        preview.producer.setCallback(null)
        runCatching {
            if (preview.player.isPlaying) preview.player.stop()
        }
        runCatching { preview.player.reset() }
        runCatching { preview.player.release() }
        preview.producer.release()
    }

    private fun notifyTargetChanged(textureId: Long, nativeWindow: Long, available: Boolean) {
        channel.invokeMethod(
            "renderTargetChanged",
            mapOf(
                "textureId" to textureId,
                "nativeTargetHandle" to nativeWindow,
                "targetKind" to "android-native-window",
                "available" to available,
            ),
        )
    }
}
