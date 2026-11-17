package org.thecrewreborn.server

import android.animation.ObjectAnimator
import android.animation.ValueAnimator
import android.app.Activity
import android.graphics.BitmapFactory
import android.graphics.Color
import android.graphics.Path
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.graphics.drawable.ShapeDrawable
import android.graphics.drawable.shapes.PathShape
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup.LayoutParams.MATCH_PARENT
import android.view.ViewGroup.LayoutParams.WRAP_CONTENT
import android.webkit.WebResourceResponse
import android.widget.Button
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView

object Brand {
    const val NIGHT = 0xFF070B12.toInt()
    const val CYAN = 0xFF2EC5FF.toInt()
    const val STEEL = 0xFF8D99AB.toInt()
    const val INK = 0xFF04121C.toInt()

    val heading: Typeface = Typeface.create("sans-serif-condensed", Typeface.BOLD_ITALIC)

    private fun dp(a: Activity, v: Float) =
        TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, v, a.resources.displayMetrics).toInt()

    private fun slanted(color: Int): ShapeDrawable {
        val p = Path().apply {
            moveTo(12f, 0f); lineTo(100f, 0f); lineTo(88f, 40f); lineTo(0f, 40f); close()
        }
        return ShapeDrawable(PathShape(p, 100f, 40f)).apply { paint.color = color }
    }

    class TitleBar(val view: View, val subtitle: TextView)

    fun titleBar(a: Activity): TitleBar {
        val words = TextView(a).apply {
            text = a.getString(R.string.app_name_mark)
            typeface = heading
            setTextColor(Color.WHITE)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 21f)
            letterSpacing = 0.03f
            includeFontPadding = false
        }
        val sub = TextView(a).apply {
            setTextColor(STEEL)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 12f)
            maxLines = 1
        }
        val texts = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_VERTICAL
            setPadding(dp(a, 10f), 0, 0, 0)
            addView(words)
            addView(sub)
        }
        val bar = View(a).apply { setBackgroundColor(CYAN) }
        val row = LinearLayout(a).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.CENTER_VERTICAL
            addView(bar, LinearLayout.LayoutParams(dp(a, 4f), dp(a, 36f)))
            addView(texts, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
        }
        return TitleBar(row, sub)
    }

    class Splash(val view: View, val status: TextView, val start: Button, private val glow: View) {
        private var pulse: ObjectAnimator? = null

        fun show(running: Boolean, text: String) {
            view.visibility = View.VISIBLE
            status.text = text
            start.visibility = if (running) View.GONE else View.VISIBLE
            if (running && pulse == null) {
                pulse = ObjectAnimator.ofFloat(glow, View.ALPHA, 0.25f, 1f).apply {
                    duration = 900
                    repeatMode = ValueAnimator.REVERSE
                    repeatCount = ValueAnimator.INFINITE
                    start()
                }
            } else if (!running) {
                pulse?.cancel()
                pulse = null
                glow.alpha = 0.6f
            }
        }

        fun hide() {
            pulse?.cancel()
            pulse = null
            view.visibility = View.GONE
        }

        val visible get() = view.visibility == View.VISIBLE
    }

    fun splash(a: Activity, onStart: () -> Unit): Splash {
        val photo = ImageView(a).apply {
            scaleType = ImageView.ScaleType.CENTER_CROP
            try {
                a.assets.open("app/splash.webp").use { setImageBitmap(BitmapFactory.decodeStream(it)) }
            } catch (e: Exception) {
            }
        }
        val shade = View(a).apply {
            background = GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                intArrayOf(0x00070B12, 0x40070B12, 0xE6070B12.toInt(), NIGHT)
            )
        }
        val tint = View(a).apply {
            background = GradientDrawable(
                GradientDrawable.Orientation.TR_BL, intArrayOf(0x402EC5FF, 0x00000000, 0x00000000)
            )
        }
        fun mark(text: String, color: Int) = TextView(a).apply {
            this.text = text
            typeface = heading
            setTextColor(color)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 54f)
            includeFontPadding = false
            setLineSpacing(0f, 0.9f)
        }
        val status = TextView(a).apply {
            setTextColor(0xFFB7C3D2.toInt())
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 16f)
            setPadding(0, dp(a, 14f), 0, dp(a, 20f))
        }
        val start = Button(a).apply {
            text = a.getString(R.string.splash_start)
            typeface = heading
            setTextColor(INK)
            setTextSize(TypedValue.COMPLEX_UNIT_SP, 20f)
            isAllCaps = true
            background = slanted(CYAN)
            stateListAnimator = null
            setPadding(dp(a, 34f), dp(a, 10f), dp(a, 34f), dp(a, 10f))
            setOnClickListener { onStart() }
        }
        val glow = View(a).apply {
            background = GradientDrawable(GradientDrawable.Orientation.LEFT_RIGHT, intArrayOf(0x002EC5FF, CYAN))
        }
        val column = LinearLayout(a).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(a, 24f), 0, dp(a, 24f), dp(a, 48f))
            addView(mark(a.getString(R.string.splash_mark_1), Color.WHITE))
            addView(mark(a.getString(R.string.splash_mark_2), CYAN))
            addView(status)
            addView(start, LinearLayout.LayoutParams(WRAP_CONTENT, WRAP_CONTENT))
            addView(glow, LinearLayout.LayoutParams(dp(a, 180f), dp(a, 3f)).apply { topMargin = dp(a, 26f) })
        }
        val root = FrameLayout(a).apply {
            setBackgroundColor(NIGHT)
            isClickable = true
            addView(photo, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
            addView(tint, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
            addView(shade, FrameLayout.LayoutParams(MATCH_PARENT, MATCH_PARENT))
            addView(column, FrameLayout.LayoutParams(MATCH_PARENT, WRAP_CONTENT, Gravity.BOTTOM))
        }
        return Splash(root, status, start, glow)
    }

    fun pageFile(a: Activity, path: String): WebResourceResponse? {
        if (!path.startsWith("/_app/")) return null
        val name = path.removePrefix("/_app/")
        if (name.contains('/') || name.contains("..")) return null
        val mime = when {
            name.endsWith(".webp") -> "image/webp"
            name.endsWith(".woff2") -> "font/woff2"
            else -> return null
        }
        return try {
            WebResourceResponse(mime, null, a.assets.open("app/$name"))
        } catch (e: Exception) {
            null
        }
    }
}
