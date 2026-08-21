package com.primedigitor.digitor_engine_ffi

import android.app.Activity
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.PixelFormat
import android.graphics.PorterDuff
import android.graphics.RectF
import android.os.Build
import android.os.Handler
import android.os.Looper
import android.util.TypedValue
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.ViewGroup
import kotlin.math.roundToInt

/**
 * Export progress surface that remains drawable while Flutter's UI isolate and
 * Android main looper are occupied by the synchronous production export call.
 *
 * The SurfaceView is attached and its Surface is created before Dart is allowed
 * to enter the blocking FFI export. Native progress callbacks then draw directly
 * into this independent CPU-backed Surface from the export thread. SurfaceFlinger
 * can composite those posted buffers without waiting for Flutter/Choreographer.
 */
internal class ExportProgressSurfaceOverlay(
    private val activity: Activity,
) : SurfaceHolder.Callback {
    private val mainHandler = Handler(Looper.getMainLooper())
    private val drawLock = Any()

    @Volatile
    private var surfaceView: SurfaceView? = null

    @Volatile
    private var surfaceHolder: SurfaceHolder? = null

    @Volatile
    private var surfaceReady = false

    @Volatile
    private var lastPercent = -1

    private var parent: ViewGroup? = null
    private var readyCallback: (() -> Unit)? = null

    fun prepare(onReady: () -> Unit) {
        if (Looper.myLooper() != Looper.getMainLooper()) {
            mainHandler.post { prepare(onReady) }
            return
        }

        dismissOnMain(invokePendingReady = false)

        if (activity.isFinishing ||
            (Build.VERSION.SDK_INT >= Build.VERSION_CODES.JELLY_BEAN_MR1 && activity.isDestroyed)
        ) {
            onReady()
            return
        }

        val content = activity.findViewById<ViewGroup>(android.R.id.content)
        if (content == null) {
            onReady()
            return
        }

        val view = SurfaceView(activity).apply {
            setZOrderOnTop(true)
            holder.setFormat(PixelFormat.TRANSLUCENT)
            isClickable = true
            isFocusable = true
            setOnTouchListener { _, _ -> true }
        }
        view.holder.addCallback(this)

        parent = content
        surfaceView = view
        surfaceHolder = view.holder
        surfaceReady = false
        lastPercent = -1
        readyCallback = onReady

        content.addView(
            view,
            ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            ),
        )
    }

    fun show() {
        drawProgress(
            percent = 0,
            completed = 0L,
            total = 0L,
            status = "Preparing export…",
            force = true,
        )
    }

    fun update(fraction: Double, completed: Long, total: Long) {
        val safeFraction = if (fraction.isFinite()) {
            fraction.coerceIn(0.0, 1.0)
        } else {
            0.0
        }
        val percent = (safeFraction * 100.0).roundToInt().coerceIn(0, 100)
        if (percent == lastPercent) return
        drawProgress(
            percent = percent,
            completed = completed,
            total = total,
            status = if (total > 0L) {
                "${completed.coerceIn(0L, total)} / $total frames"
            } else {
                "Encoding video…"
            },
            force = false,
        )
    }

    fun complete() {
        drawProgress(
            percent = 100,
            completed = 0L,
            total = 0L,
            status = "Finalizing export…",
            force = true,
        )
    }

    fun dismiss() {
        if (Looper.myLooper() == Looper.getMainLooper()) {
            dismissOnMain(invokePendingReady = true)
        } else {
            mainHandler.post { dismissOnMain(invokePendingReady = true) }
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        surfaceHolder = holder
        surfaceReady = true
        lastPercent = -1
        show()

        val callback = readyCallback
        readyCallback = null
        callback?.invoke()
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int,
    ) {
        surfaceHolder = holder
        surfaceReady = width > 0 && height > 0
        if (surfaceReady && lastPercent < 0) show()
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        surfaceReady = false
        if (surfaceHolder === holder) surfaceHolder = null
    }

    private fun drawProgress(
        percent: Int,
        completed: Long,
        total: Long,
        status: String,
        force: Boolean,
    ) {
        if (!surfaceReady) return
        if (!force && percent == lastPercent) return

        synchronized(drawLock) {
            val holder = surfaceHolder ?: return
            if (!surfaceReady || !holder.surface.isValid) return

            var canvas: Canvas? = null
            try {
                canvas = holder.lockCanvas()
                if (canvas == null || canvas.width <= 0 || canvas.height <= 0) return
                render(canvas, percent, completed, total, status)
                lastPercent = percent
            } catch (_: Throwable) {
                // Export must never fail because its progress surface was lost.
            } finally {
                if (canvas != null) {
                    runCatching { holder.unlockCanvasAndPost(canvas) }
                }
            }
        }
    }

    private fun render(
        canvas: Canvas,
        percent: Int,
        completed: Long,
        total: Long,
        status: String,
    ) {
        canvas.drawColor(Color.TRANSPARENT, PorterDuff.Mode.CLEAR)
        canvas.drawColor(Color.argb(170, 0, 0, 0))

        val density = activity.resources.displayMetrics.density
        val scaledDensity = activity.resources.displayMetrics.scaledDensity
        val width = canvas.width.toFloat()
        val height = canvas.height.toFloat()

        val cardWidth = minOf(width * 0.84f, 430f * density)
        val cardHeight = 176f * density
        val left = (width - cardWidth) * 0.5f
        val top = (height - cardHeight) * 0.5f
        val right = left + cardWidth
        val bottom = top + cardHeight
        val card = RectF(left, top, right, bottom)

        val paint = Paint(Paint.ANTI_ALIAS_FLAG)
        paint.color = Color.rgb(30, 30, 30)
        canvas.drawRoundRect(card, 18f * density, 18f * density, paint)

        val padding = 22f * density
        paint.color = Color.WHITE
        paint.textSize = 19f * scaledDensity
        paint.typeface = android.graphics.Typeface.DEFAULT_BOLD
        canvas.drawText(
            "Exporting video — ${percent.coerceIn(0, 100)}%",
            left + padding,
            top + 42f * density,
            paint,
        )

        paint.typeface = android.graphics.Typeface.DEFAULT
        paint.color = Color.rgb(200, 200, 200)
        paint.textSize = 14f * scaledDensity
        val detail = if (total > 0L) {
            "${completed.coerceIn(0L, total)} / $total frames"
        } else {
            status
        }
        canvas.drawText(detail, left + padding, top + 72f * density, paint)

        val trackTop = top + 100f * density
        val trackBottom = trackTop + 12f * density
        val trackLeft = left + padding
        val trackRight = right - padding
        val track = RectF(trackLeft, trackTop, trackRight, trackBottom)

        paint.color = Color.rgb(70, 70, 70)
        canvas.drawRoundRect(track, 6f * density, 6f * density, paint)

        val fillFraction = percent.coerceIn(0, 100) / 100f
        if (fillFraction > 0f) {
            val fill = RectF(
                trackLeft,
                trackTop,
                trackLeft + (trackRight - trackLeft) * fillFraction,
                trackBottom,
            )
            paint.color = resolveAccentColor()
            canvas.drawRoundRect(fill, 6f * density, 6f * density, paint)
        }

        paint.color = Color.rgb(170, 170, 170)
        paint.textSize = 12f * scaledDensity
        canvas.drawText(status, left + padding, bottom - 22f * density, paint)
    }

    private fun resolveAccentColor(): Int {
        val value = TypedValue()
        return if (activity.theme.resolveAttribute(android.R.attr.colorAccent, value, true)) {
            value.data
        } else {
            Color.rgb(76, 175, 80)
        }
    }

    private fun dismissOnMain(invokePendingReady: Boolean) {
        val pending = readyCallback
        readyCallback = null
        if (invokePendingReady) pending?.invoke()

        surfaceReady = false
        lastPercent = -1

        val view = surfaceView
        val holder = surfaceHolder
        surfaceView = null
        surfaceHolder = null

        if (holder != null) {
            runCatching { holder.removeCallback(this) }
        }
        if (view != null) {
            runCatching { (view.parent as? ViewGroup)?.removeView(view) }
        }
        parent = null
    }
}
