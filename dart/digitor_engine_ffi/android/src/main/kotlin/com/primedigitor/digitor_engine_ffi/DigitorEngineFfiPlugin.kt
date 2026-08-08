package com.primedigitor.digitor_engine_ffi

import android.view.Surface
import androidx.annotation.Keep
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

    private lateinit var channel: MethodChannel
    private lateinit var textures: TextureRegistry
    private val hosts = mutableMapOf<Long, HostTexture>()

    @Keep
    private external fun nativeAcquireWindow(surface: Surface): Long

    @Keep
    private external fun nativeReleaseWindow(handle: Long)

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        textures = binding.textureRegistry
        channel = MethodChannel(binding.binaryMessenger, CHANNEL)
        channel.setMethodCallHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
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
