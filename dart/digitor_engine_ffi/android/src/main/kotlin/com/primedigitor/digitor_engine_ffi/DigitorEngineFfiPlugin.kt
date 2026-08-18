package com.primedigitor.digitor_engine_ffi

import android.content.ContentValues
import android.content.Context
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import android.view.Surface
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.view.TextureRegistry
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream

class DigitorEngineFfiPlugin : FlutterPlugin, MethodChannel.MethodCallHandler {
    companion object {
        private const val CHANNEL = "digitor_engine_ffi/platform_host"
        private const val EXPORT_DIR = "digitor-exports"
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
    private lateinit var context: Context
    private val hosts = mutableMapOf<Long, HostTexture>()

    private external fun nativeAcquireWindow(surface: Surface): Long

    private external fun nativeReleaseWindow(handle: Long)

    private external fun nativeProductionRegistrarToken(): Long

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
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
            "prepareExport" -> prepareExport(call, result)
            "publishExport" -> publishExport(call, result)
            "discardExport" -> discardExport(call, result)
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

    private fun exportStagingDirectory(): File {
        val directory = File(context.cacheDir, EXPORT_DIR)
        if (!directory.exists() && !directory.mkdirs()) {
            throw IllegalStateException("Could not create Android export staging directory.")
        }
        return directory
    }

    private fun safeExportName(raw: String?): String {
        val base = raw
            ?.substringAfterLast('/')
            ?.substringAfterLast('\\')
            ?.trim()
            ?.takeIf { it.isNotEmpty() }
            ?: "digitor-export.mp4"
        val cleaned = base.replace(Regex("[^A-Za-z0-9._-]"), "_")
        return if (cleaned.lowercase().endsWith(".mp4")) cleaned else "$cleaned.mp4"
    }

    private fun prepareExport(call: MethodCall, result: MethodChannel.Result) {
        try {
            val requested = safeExportName(call.argument<String>("displayName"))
            val stem = requested.removeSuffix(".mp4")
            val displayName = "$stem-${System.currentTimeMillis()}.mp4"
            val staging = File(exportStagingDirectory(), displayName)
            if (staging.exists()) staging.delete()
            File("${staging.absolutePath}.digitor-partial").delete()
            result.success(
                mapOf(
                    "stagingPath" to staging.absolutePath,
                    "displayName" to displayName,
                    "collection" to "Movies/Digitor",
                ),
            )
        } catch (error: Throwable) {
            result.error("export_staging_failed", error.message ?: "Could not prepare export staging.", null)
        }
    }

    private fun validatedStagingFile(path: String?): File {
        require(!path.isNullOrBlank()) { "stagingPath is required." }
        val root = exportStagingDirectory().canonicalFile
        val file = File(path).canonicalFile
        require(file.parentFile == root) { "Export staging path is outside the plugin cache." }
        require(file.isFile && file.length() > 0L) { "Encoded staging MP4 is missing or empty." }
        return file
    }

    private fun publishExport(call: MethodCall, result: MethodChannel.Result) {
        var insertedUri: android.net.Uri? = null
        try {
            val staging = validatedStagingFile(call.argument<String>("stagingPath"))
            val displayName = safeExportName(call.argument<String>("displayName") ?: staging.name)

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                val resolver = context.contentResolver
                val values = ContentValues().apply {
                    put(MediaStore.Video.Media.DISPLAY_NAME, displayName)
                    put(MediaStore.Video.Media.MIME_TYPE, "video/mp4")
                    put(
                        MediaStore.Video.Media.RELATIVE_PATH,
                        "${Environment.DIRECTORY_MOVIES}/Digitor",
                    )
                    put(MediaStore.Video.Media.IS_PENDING, 1)
                }
                insertedUri = resolver.insert(MediaStore.Video.Media.EXTERNAL_CONTENT_URI, values)
                    ?: throw IllegalStateException("MediaStore could not create the Digitor export item.")
                resolver.openOutputStream(insertedUri, "w")?.use { output ->
                    FileInputStream(staging).use { input -> input.copyTo(output) }
                } ?: throw IllegalStateException("MediaStore export stream is unavailable.")
                val publishValues = ContentValues().apply {
                    put(MediaStore.Video.Media.IS_PENDING, 0)
                }
                resolver.update(insertedUri, publishValues, null, null)
                if (!staging.delete()) staging.deleteOnExit()
                result.success(
                    mapOf(
                        "uri" to insertedUri.toString(),
                        "displayPath" to "${Environment.DIRECTORY_MOVIES}/Digitor/$displayName",
                        "displayName" to displayName,
                    ),
                )
                return
            }

            val root = context.getExternalFilesDir(Environment.DIRECTORY_MOVIES)
                ?: throw IllegalStateException("Android external Movies directory is unavailable.")
            val directory = File(root, "Digitor")
            if (!directory.exists() && !directory.mkdirs()) {
                throw IllegalStateException("Could not create the Digitor Movies directory.")
            }
            val destination = File(directory, displayName)
            FileInputStream(staging).use { input ->
                FileOutputStream(destination, false).use { output -> input.copyTo(output) }
            }
            if (!staging.delete()) staging.deleteOnExit()
            result.success(
                mapOf(
                    "uri" to destination.toURI().toString(),
                    "displayPath" to destination.absolutePath,
                    "displayName" to displayName,
                ),
            )
        } catch (error: Throwable) {
            insertedUri?.let { runCatching { context.contentResolver.delete(it, null, null) } }
            result.error("export_publish_failed", error.message ?: "Could not publish Android export.", null)
        }
    }

    private fun discardExport(call: MethodCall, result: MethodChannel.Result) {
        try {
            val raw = call.argument<String>("stagingPath")
            if (!raw.isNullOrBlank()) {
                val root = exportStagingDirectory().canonicalFile
                val file = File(raw).canonicalFile
                if (file.parentFile == root) {
                    file.delete()
                    File("${file.absolutePath}.digitor-partial").delete()
                }
            }
            result.success(null)
        } catch (error: Throwable) {
            result.error("export_discard_failed", error.message ?: "Could not discard Android export staging.", null)
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
