package com.primedigitor.digitor_engine_ffi

import android.app.Activity
import android.app.ProgressDialog
import android.content.ContentValues
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.MediaStore
import android.provider.OpenableColumns
import android.view.Surface
import android.webkit.MimeTypeMap
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.embedding.engine.plugins.activity.ActivityAware
import io.flutter.embedding.engine.plugins.activity.ActivityPluginBinding
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.PluginRegistry
import io.flutter.view.TextureRegistry
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.util.UUID
import java.util.concurrent.Executors
import kotlin.math.roundToInt

class DigitorEngineFfiPlugin : FlutterPlugin,
    MethodChannel.MethodCallHandler,
    ActivityAware,
    PluginRegistry.ActivityResultListener {
    companion object {
        private const val CHANNEL = "digitor_engine_ffi/platform_host"
        private const val EXPORT_DIR = "digitor-exports"
        private const val IMPORT_DIR = "digitor-imports"
        private const val IMPORT_REQUEST_CODE = 0xD617
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
    private var activityBinding: ActivityPluginBinding? = null
    private var pendingImportResult: MethodChannel.Result? = null
    private val importExecutor = Executors.newSingleThreadExecutor()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val hosts = mutableMapOf<Long, HostTexture>()
    @Suppress("DEPRECATION")
    private var exportProgressDialog: ProgressDialog? = null

    private external fun nativeAcquireWindow(surface: Surface): Long

    private external fun nativeReleaseWindow(handle: Long)

    private external fun nativeProductionRegistrarToken(): Long

    private external fun nativeReleaseProductionRegistrarToken()

    override fun onAttachedToEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        context = binding.applicationContext
        textures = binding.textureRegistry
        channel = MethodChannel(binding.binaryMessenger, CHANNEL)
        channel.setMethodCallHandler(this)
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        channel.setMethodCallHandler(null)
        dismissExportProgressDialog()
        nativeReleaseProductionRegistrarToken()
        hosts.keys.toList().forEach(::disposeTexture)
        pendingImportResult?.error(
            "media_import_detached",
            "Android media import host detached before selection completed.",
            null,
        )
        pendingImportResult = null
        importExecutor.shutdownNow()
    }

    override fun onAttachedToActivity(binding: ActivityPluginBinding) {
        activityBinding = binding
        binding.addActivityResultListener(this)
    }

    override fun onDetachedFromActivityForConfigChanges() {
        activityBinding?.removeActivityResultListener(this)
        dismissExportProgressDialog()
        activityBinding = null
    }

    override fun onReattachedToActivityForConfigChanges(binding: ActivityPluginBinding) {
        onAttachedToActivity(binding)
    }

    override fun onDetachedFromActivity() {
        activityBinding?.removeActivityResultListener(this)
        dismissExportProgressDialog()
        activityBinding = null
        pendingImportResult?.error(
            "media_import_activity_detached",
            "Android activity detached before media selection completed.",
            null,
        )
        pendingImportResult = null
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
            "pickMediaImport" -> pickMediaImport(result)
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

    @Suppress("unused", "DEPRECATION")
    fun showExportProgressFromNative() {
        mainHandler.post {
            val dialog = ensureExportProgressDialog() ?: return@post
            dialog.max = 100
            dialog.progress = 0
            dialog.setTitle("Exporting video — 0%")
            dialog.setMessage("Preparing export…")
        }
    }

    @Suppress("unused", "DEPRECATION")
    fun updateExportProgressFromNative(fraction: Double, completed: Long, total: Long) {
        mainHandler.post {
            val dialog = ensureExportProgressDialog() ?: return@post
            val safeFraction = if (fraction.isFinite()) fraction.coerceIn(0.0, 1.0) else 0.0
            val percent = (safeFraction * 100.0).roundToInt().coerceIn(0, 100)
            dialog.progress = percent
            dialog.setTitle("Exporting video — $percent%")
            dialog.setMessage(
                if (total > 0L) {
                    "${completed.coerceIn(0L, total)} / $total frames"
                } else {
                    "Encoding video…"
                },
            )
        }
    }

    @Suppress("unused", "DEPRECATION")
    fun hideExportProgressFromNative(resultCode: Int) {
        mainHandler.post {
            val dialog = exportProgressDialog ?: return@post
            if (resultCode == 0) {
                dialog.progress = 100
                dialog.setTitle("Exporting video — 100%")
                dialog.setMessage("Finalizing export…")
                mainHandler.postDelayed({ dismissExportProgressDialog() }, 400L)
            } else {
                dismissExportProgressDialog()
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun ensureExportProgressDialog(): ProgressDialog? {
        val existing = exportProgressDialog
        if (existing?.isShowing == true) return existing

        val activity = activityBinding?.activity ?: return null
        if (activity.isFinishing ||
            (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1 && activity.isDestroyed)
        ) {
            return null
        }

        val dialog = ProgressDialog(activity).apply {
            setProgressStyle(ProgressDialog.STYLE_HORIZONTAL)
            max = 100
            progress = 0
            isIndeterminate = false
            setTitle("Exporting video — 0%")
            setMessage("Preparing export…")
            setCancelable(false)
            setCanceledOnTouchOutside(false)
            show()
        }
        exportProgressDialog = dialog
        return dialog
    }

    @Suppress("DEPRECATION")
    private fun dismissExportProgressDialog() {
        val dialog = exportProgressDialog
        exportProgressDialog = null
        if (dialog != null) {
            runCatching {
                if (dialog.isShowing) dialog.dismiss()
            }
        }
    }

    private fun importStagingDirectory(): File {
        val directory = File(context.cacheDir, IMPORT_DIR)
        if (!directory.exists() && !directory.mkdirs()) {
            throw IllegalStateException("Could not create Android import staging directory.")
        }
        return directory
    }

    private fun pickMediaImport(result: MethodChannel.Result) {
        if (pendingImportResult != null) {
            result.error("media_import_busy", "Another Android media picker is already active.", null)
            return
        }
        val binding = activityBinding
        if (binding == null) {
            result.error("media_import_no_activity", "No Android activity is available for media import.", null)
            return
        }

        pendingImportResult = result
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "video/*"
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        try {
            binding.activity.startActivityForResult(intent, IMPORT_REQUEST_CODE)
        } catch (error: Throwable) {
            pendingImportResult = null
            result.error(
                "media_import_picker_failed",
                error.message ?: "Could not launch Android media picker.",
                null,
            )
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode != IMPORT_REQUEST_CODE) return false
        val callback = pendingImportResult ?: return true
        pendingImportResult = null
        val uri = if (resultCode == Activity.RESULT_OK) data?.data else null
        if (uri == null) {
            callback.success(null)
            return true
        }

        importExecutor.execute {
            try {
                val imported = copyImportToCache(uri)
                mainHandler.post { callback.success(imported) }
            } catch (error: Throwable) {
                mainHandler.post {
                    callback.error(
                        "media_import_copy_failed",
                        error.message ?: "Could not stage selected Android media.",
                        null,
                    )
                }
            }
        }
        return true
    }

    private fun selectedDisplayName(uri: Uri): String? {
        return runCatching {
            context.contentResolver.query(
                uri,
                arrayOf(OpenableColumns.DISPLAY_NAME),
                null,
                null,
                null,
            )?.use { cursor ->
                if (!cursor.moveToFirst()) return@use null
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0) cursor.getString(index) else null
            }
        }.getOrNull()
    }

    private fun importExtension(uri: Uri, displayName: String?): String {
        val fromName = displayName
            ?.substringAfterLast('.', "")
            ?.lowercase()
            ?.takeIf { it.matches(Regex("[a-z0-9]{1,8}")) }
        if (fromName != null) return fromName
        val mime = context.contentResolver.getType(uri)
        return MimeTypeMap.getSingleton().getExtensionFromMimeType(mime) ?: "mp4"
    }

    private fun copyImportToCache(uri: Uri): Map<String, Any> {
        val resolver = context.contentResolver
        val displayName = selectedDisplayName(uri) ?: "imported-video"
        val extension = importExtension(uri, displayName)
        val destination = File(
            importStagingDirectory(),
            "import-${System.currentTimeMillis()}-${UUID.randomUUID()}.$extension",
        )

        resolver.openInputStream(uri)?.use { input ->
            FileOutputStream(destination, false).use { output ->
                input.copyTo(output, 1024 * 1024)
                output.fd.sync()
            }
        } ?: throw IllegalStateException("Android selected media stream is unavailable.")

        if (!destination.isFile || destination.length() <= 0L) {
            destination.delete()
            throw IllegalStateException("Android selected media copy is empty.")
        }

        // Keep only the current import plus files that may still be in use very recently.
        val cutoff = System.currentTimeMillis() - 6L * 60L * 60L * 1000L
        destination.parentFile?.listFiles()?.forEach { candidate ->
            if (candidate != destination && candidate.isFile && candidate.lastModified() < cutoff) {
                candidate.delete()
            }
        }

        return mapOf(
            "path" to destination.absolutePath,
            "displayName" to displayName,
            "mimeType" to (resolver.getType(uri) ?: "video/*"),
            "size" to destination.length(),
        )
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
                    FileInputStream(staging).use { input -> input.copyTo(output, 1024 * 1024) }
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
                FileOutputStream(destination, false).use { output -> input.copyTo(output, 1024 * 1024) }
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
