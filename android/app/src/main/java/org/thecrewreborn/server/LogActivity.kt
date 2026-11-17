package org.thecrewreborn.server

import android.app.Activity
import android.content.Intent
import android.graphics.Typeface
import android.os.Bundle
import android.view.Menu
import android.view.MenuItem
import android.widget.ScrollView
import android.widget.TextView
import java.io.RandomAccessFile

class LogActivity : Activity() {

    private lateinit var text: TextView
    private lateinit var scroll: ScrollView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        text = TextView(this).apply {
            typeface = Typeface.MONOSPACE
            textSize = 11f
            setTextIsSelectable(true)
            val pad = (8 * resources.displayMetrics.density).toInt()
            setPadding(pad, pad, pad, pad)
        }
        scroll = ScrollView(this).apply { addView(text) }
        setContentView(scroll)
        refresh()
    }

    private fun tail(): String {
        val f = ServerFiles(this).logFile
        if (!f.exists()) return ""
        RandomAccessFile(f, "r").use { r ->
            val n = minOf(r.length(), 256L * 1024)
            r.seek(r.length() - n)
            val b = ByteArray(n.toInt())
            r.readFully(b)
            return String(b, Charsets.UTF_8)
        }
    }

    private fun refresh() {
        text.text = tail()
        scroll.post { scroll.fullScroll(ScrollView.FOCUS_DOWN) }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menu.add(0, 1, 0, R.string.menu_refresh)
        menu.add(0, 2, 0, R.string.menu_share)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            1 -> refresh()
            2 -> startActivity(
                Intent.createChooser(
                    Intent(Intent.ACTION_SEND).setType("text/plain").putExtra(Intent.EXTRA_TEXT, tail()), null
                )
            )
            else -> return super.onOptionsItemSelected(item)
        }
        return true
    }
}
