package org.thecrewreborn.server

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.PowerManager
import android.provider.Settings
import android.view.Menu
import android.view.MenuItem
import android.webkit.CookieManager
import android.webkit.URLUtil
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Toast
import java.net.HttpURLConnection
import java.net.URL

class MainActivity : Activity() {

    companion object {
        private const val REQUEST_SAVE = 2
    }

    private lateinit var web: WebView
    private val main = Handler(Looper.getMainLooper())
    private var retry: Runnable? = null
    private var pendingDownload: String? = null

    private var darkShown = false
    private lateinit var titleBar: Brand.TitleBar
    private lateinit var splash: Brand.Splash
    private var pageFailed = false
    private val saves by lazy { SavesUi(this) { scheduleRetry() } }

    private fun themeSetting(): String =
        getSharedPreferences("app", MODE_PRIVATE).getString("theme", "system") ?: "system"

    private fun isDark(): Boolean = when (themeSetting()) {
        "dark" -> true
        "light" -> false
        else -> (resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK) ==
            Configuration.UI_MODE_NIGHT_YES
    }

    private fun chooseTheme() {
        val names = arrayOf(getString(R.string.theme_system), getString(R.string.theme_light), getString(R.string.theme_dark))
        val values = arrayOf("system", "light", "dark")
        android.app.AlertDialog.Builder(this)
            .setTitle(R.string.menu_theme)
            .setSingleChoiceItems(names, values.indexOf(themeSetting())) { dialog, which ->
                getSharedPreferences("app", MODE_PRIVATE).edit().putString("theme", values[which]).apply()
                dialog.dismiss()
                recreate()
            }
            .show()
    }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        if (isDark() != darkShown) recreate()
    }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        darkShown = isDark()
        setTheme(if (darkShown) android.R.style.Theme_DeviceDefault
                 else android.R.style.Theme_DeviceDefault_Light_DarkActionBar)
        super.onCreate(savedInstanceState)
        window.statusBarColor = Brand.NIGHT
        titleBar = Brand.titleBar(this)
        actionBar?.apply {
            setBackgroundDrawable(android.graphics.drawable.ColorDrawable(Brand.NIGHT))
            setDisplayShowTitleEnabled(false)
            setDisplayShowCustomEnabled(true)
            customView = titleBar.view
        }
        web = WebView(this)
        web.setBackgroundColor(if (darkShown) Brand.NIGHT else 0xFFF3F5F8.toInt())
        web.settings.javaScriptEnabled = true
        web.settings.domStorageEnabled = true
        web.webViewClient = object : WebViewClient() {
            override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
                if (request.isForMainFrame) {
                    pageFailed = true
                    scheduleRetry()
                }
            }

            override fun onPageStarted(view: WebView, url: String, favicon: Bitmap?) {
                pageFailed = false
                cancelRetry()
            }

            override fun onPageFinished(view: WebView, url: String) {
                if (!pageFailed && url.startsWith("http")) splash.hide()
            }

            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? {
                val uri = request.url
                if (uri.host != "127.0.0.1") return null
                return Brand.pageFile(this@MainActivity, uri.path ?: return null)
            }

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                val uri = request.url
                if (uri.host == "127.0.0.1" || uri.host == "localhost") return false
                try {
                    startActivity(Intent(Intent.ACTION_VIEW, uri))
                } catch (e: Exception) {
                }
                return true
            }
        }
        web.webChromeClient = WebChromeClient()
        web.setDownloadListener { url, _, contentDisposition, mimetype, _ ->
            pendingDownload = url
            val name = URLUtil.guessFileName(url, contentDisposition, mimetype)
            val pick = Intent(Intent.ACTION_CREATE_DOCUMENT)
                .addCategory(Intent.CATEGORY_OPENABLE)
                .setType(mimetype?.takeIf { it.isNotEmpty() } ?: "application/octet-stream")
                .putExtra(Intent.EXTRA_TITLE, Uri.parse(url).getQueryParameter("file") ?: name)
            startActivityForResult(pick, REQUEST_SAVE)
        }
        splash = Brand.splash(this) {
            ServerService.start(this)
            scheduleRetry()
        }
        setContentView(android.widget.FrameLayout(this).apply {
            addView(web)
            addView(splash.view)
        })
        askNotifications()
        scheduleRetry()
    }

    override fun onDestroy() {
        cancelRetry()
        web.destroy()
        super.onDestroy()
    }

    @Deprecated("Activity.onBackPressed")
    override fun onBackPressed() {
        if (web.canGoBack()) web.goBack() else super.onBackPressed()
    }

    @Deprecated("Activity.onActivityResult")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (saves.onActivityResult(requestCode, resultCode, data)) return
        val url = pendingDownload
        pendingDownload = null
        val target = data?.data
        if (requestCode != REQUEST_SAVE || resultCode != RESULT_OK || url == null || target == null) return
        val cookie = CookieManager.getInstance().getCookie(url)
        Thread({
            val ok = try {
                val c = URL(url).openConnection() as HttpURLConnection
                if (cookie != null) c.setRequestProperty("Cookie", cookie)
                c.inputStream.use { input ->
                    contentResolver.openOutputStream(target)!!.use { input.copyTo(it) }
                }
                true
            } catch (e: Exception) {
                false
            }
            runOnUiThread {
                Toast.makeText(this, if (ok) R.string.download_done else R.string.download_failed, Toast.LENGTH_SHORT)
                    .show()
            }
        }, "download").start()
    }

    private fun pageUrl() =
        "http://127.0.0.1:${ServerService.adminPort(this)}/?app=1&theme=" + if (darkShown) "dark" else "light"

    private var shownRunning: Boolean? = null
    private val stateTick = object : Runnable {
        override fun run() {
            val running = ServerService.running
            titleBar.subtitle.text = if (running) {
                getString(R.string.state_running) + (ServerService.address?.let { " · $it" } ?: "")
            } else {
                getString(R.string.state_stopped)
            }
            if (!running && !splash.visible) scheduleRetry()
            if (running && splash.visible && retry == null) scheduleRetry()
            if (running != shownRunning) {
                shownRunning = running
                invalidateOptionsMenu()
            }
            main.postDelayed(this, 1000)
        }
    }

    override fun onResume() {
        super.onResume()
        main.post(stateTick)
        saves.onResume()
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        saves.onRequestPermissionsResult(requestCode)
    }

    override fun onPause() {
        main.removeCallbacks(stateTick)
        super.onPause()
    }

    private fun clickInPage(id: String) {
        web.evaluateJavascript("(function(){var b=document.getElementById('$id');if(b)b.click();})()", null)
    }

    private fun loadPage() {
        web.loadUrl(pageUrl())
    }

    private fun scheduleRetry() {
        cancelRetry()
        val running = ServerService.running
        splash.show(running, getString(if (running) R.string.waiting else R.string.stopped_hint))
        if (!running) return
        val r = Runnable {
            retry = null
            loadPage()
        }
        retry = r
        main.postDelayed(r, 1000)
    }

    private fun cancelRetry() {
        retry?.let { main.removeCallbacks(it) }
        retry = null
    }

    private fun askNotifications() {
        if (Build.VERSION.SDK_INT >= 33 &&
            checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
        ) {
            requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menu.add(0, 4, 0, R.string.menu_stop).setShowAsAction(MenuItem.SHOW_AS_ACTION_ALWAYS)
        menu.add(0, 6, 1, R.string.menu_restart)
        menu.add(0, 7, 2, R.string.menu_language)
        menu.add(0, 8, 3, R.string.menu_export)
        menu.add(0, 9, 3, R.string.menu_import)
        menu.add(0, 10, 3, R.string.menu_folder)
        menu.add(0, 5, 3, R.string.menu_theme)
        menu.add(0, 2, 4, R.string.menu_log)
        menu.add(0, 3, 5, R.string.menu_battery)
        menu.add(0, 1, 6, R.string.menu_reload)
        return true
    }

    override fun onPrepareOptionsMenu(menu: Menu): Boolean {
        val running = ServerService.running
        menu.findItem(4)?.apply {
            setTitle(if (running) R.string.menu_stop else R.string.menu_start)
            setIcon(if (running) R.drawable.ic_stop else R.drawable.ic_play)
        }
        menu.findItem(6)?.isEnabled = running
        menu.findItem(7)?.isEnabled = running
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            1 -> loadPage()
            2 -> startActivity(Intent(this, LogActivity::class.java))
            3 -> askBatteryExemption()
            5 -> chooseTheme()
            6 -> clickInPage("restart")
            7 -> clickInPage("lang")
            8 -> saves.export()
            9 -> saves.import()
            10 -> saves.chooseFolder()
            4 -> if (ServerService.running) {
                android.app.AlertDialog.Builder(this)
                    .setMessage(R.string.confirm_stop)
                    .setPositiveButton(R.string.action_stop) { _, _ ->
                        ServerService.stop(this)
                        main.postDelayed({ scheduleRetry() }, 1500)
                    }
                    .setNegativeButton(android.R.string.cancel, null)
                    .show()
            } else {
                ServerService.start(this)
                scheduleRetry()
            }
            else -> return super.onOptionsItemSelected(item)
        }
        return true
    }

    @SuppressLint("BatteryLife")
    private fun askBatteryExemption() {
        val pm = getSystemService(PowerManager::class.java)
        if (pm.isIgnoringBatteryOptimizations(packageName)) {
            Toast.makeText(this, R.string.battery_done, Toast.LENGTH_LONG).show()
            return
        }
        try {
            startActivity(
                Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName"))
            )
        } catch (e: Exception) {
            startActivity(Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS))
        }
    }
}
