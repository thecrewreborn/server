package org.thecrewreborn.server

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.PowerManager
import java.io.File
import java.net.Inet4Address
import java.net.NetworkInterface
import java.util.TimeZone

class ServerService : Service() {

    companion object {
        const val ACTION_START = "org.thecrewreborn.server.START"
        const val ACTION_STOP = "org.thecrewreborn.server.STOP"
        private const val CHANNEL = "server"
        private const val NOTIFICATION_ID = 1
        private const val LOG_LIMIT = 2L * 1024 * 1024

        fun adminPort(context: Context): Int = ServerFiles(context).adminPort()

        @Volatile
        var running = false
            private set

        @Volatile
        var address: String? = null
            private set

        fun start(context: Context) {
            val i = Intent(context, ServerService::class.java).setAction(ACTION_START)
            context.startForegroundService(i)
        }

        fun stop(context: Context) {
            val i = Intent(context, ServerService::class.java).setAction(ACTION_STOP)
            context.startService(i)
        }
    }

    private val main = Handler(Looper.getMainLooper())
    private lateinit var files: ServerFiles
    private var process: Process? = null
    private var stopping = false
    private var wakeLock: PowerManager.WakeLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var multicastLock: WifiManager.MulticastLock? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        files = ServerFiles(this)
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.channel_name), NotificationManager.IMPORTANCE_LOW)
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        goForeground(getString(R.string.state_starting))
        when (intent?.action) {
            ACTION_STOP -> stopServer(userStop = true)
            else -> startServer()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopServer(userStop = false)
        super.onDestroy()
    }

    private fun startServer() {
        if (process != null) {
            updateNotification()
            return
        }
        stopping = false
        files = ServerFiles(this)
        try {
            files.prepare()
        } catch (e: Exception) {
            files.logFile.parentFile?.mkdirs()
            files.logFile.appendText("--- app: cannot use the server folder ${files.home}: $e\n")
            goForeground(getString(R.string.state_folder_error), stopped = true)
            stopForeground(STOP_FOREGROUND_DETACH)
            stopSelf()
            return
        }
        files.killStale()
        val log = files.logFile
        if (log.length() > LOG_LIMIT) log.delete()
        val exe = File(applicationInfo.nativeLibraryDir, "libtcreborn.so")
        val pb = ProcessBuilder(files.startCommand(exe))
            .directory(files.home)
            .redirectErrorStream(true)
            .redirectOutput(ProcessBuilder.Redirect.appendTo(log))
        pb.environment()["TCREBORN_HOME"] = files.home.absolutePath
        pb.environment()["TZ"] = posixTimeZone()
        pb.environment()["HOME"] = files.home.absolutePath
        val p = try {
            log.appendText("\n--- app: starting ${exe.absolutePath}\n")
            pb.start()
        } catch (e: Exception) {
            log.appendText("--- app: cannot start the server: $e\n")
            goForeground(getString(R.string.state_exited, -1), stopped = true)
            return
        }
        process = p
        running = true
        acquireLocks()
        updateNotification()
        Thread({
            val code = p.waitFor()
            main.post { onExited(p, code) }
        }, "server-wait").start()
    }

    private fun onExited(p: Process, code: Int) {
        if (process !== p) return
        process = null
        running = false
        files.clearPid()
        releaseLocks()
        files.logFile.appendText("--- app: the server ended with exit code $code\n")
        if (stopping) {
            finishService()
        } else {
            val state = if (code == 0) getString(R.string.state_stopped) else getString(R.string.state_exited, code)
            goForeground(state, stopped = true)
            stopForeground(STOP_FOREGROUND_DETACH)
            stopSelf()
        }
    }

    private fun stopServer(userStop: Boolean) {
        stopping = true
        val p = process
        if (p == null) {
            if (userStop) finishService()
            return
        }
        val pid = files.pid()
        if (pid != null) android.os.Process.sendSignal(pid, 15) else p.destroy()
        Thread({
            if (!waitFor(p, 5000)) p.destroyForcibly()
        }, "server-stop").start()
    }

    private fun waitFor(p: Process, ms: Long): Boolean {
        return p.waitFor(ms, java.util.concurrent.TimeUnit.MILLISECONDS)
    }

    private fun finishService() {
        releaseLocks()
        stopForeground(STOP_FOREGROUND_REMOVE)
        stopSelf()
    }

    private fun updateNotification() {
        val addr = lanAddress()
        val https = files.httpsPort()
        address = if (addr != null) "$addr:$https" else null
        val text = if (addr != null) "${getString(R.string.state_running)} · $addr:$https" else getString(R.string.state_running)
        goForeground(text)
    }

    private fun goForeground(text: String, stopped: Boolean = false) {
        val open = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE
        )
        val actionIntent = Intent(this, ServerService::class.java).setAction(if (stopped) ACTION_START else ACTION_STOP)
        val action = if (stopped) {
            PendingIntent.getForegroundService(this, 1, actionIntent, PendingIntent.FLAG_IMMUTABLE)
        } else {
            PendingIntent.getService(this, 2, actionIntent, PendingIntent.FLAG_IMMUTABLE)
        }
        val n = Notification.Builder(this, CHANNEL)
            .setSmallIcon(R.drawable.ic_notification)
            .setContentTitle(getString(R.string.app_name))
            .setContentText(text)
            .setContentIntent(open)
            .setOngoing(!stopped)
            .setShowWhen(false)
            .addAction(
                Notification.Action.Builder(
                    null, getString(if (stopped) R.string.action_start else R.string.action_stop), action
                ).build()
            )
            .build()
        if (Build.VERSION.SDK_INT >= 34) {
            startForeground(NOTIFICATION_ID, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE)
        } else {
            startForeground(NOTIFICATION_ID, n)
        }
    }

    @Suppress("DEPRECATION")
    private fun acquireLocks() {
        if (wakeLock == null) {
            wakeLock = getSystemService(PowerManager::class.java)
                .newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "TheCrewReborn:server")
                .apply { setReferenceCounted(false); acquire() }
        }
        val wifi = applicationContext.getSystemService(WifiManager::class.java)
        if (wifiLock == null) {
            wifiLock = wifi.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "TheCrewReborn:server")
                .apply { setReferenceCounted(false); acquire() }
        }
        if (multicastLock == null) {
            multicastLock = wifi.createMulticastLock("TheCrewReborn:discovery")
                .apply { setReferenceCounted(false); acquire() }
        }
    }

    private fun releaseLocks() {
        wakeLock?.let { if (it.isHeld) it.release() }
        wifiLock?.let { if (it.isHeld) it.release() }
        multicastLock?.let { if (it.isHeld) it.release() }
        wakeLock = null
        wifiLock = null
        multicastLock = null
    }

    private fun lanAddress(): String? {
        return try {
            NetworkInterface.getNetworkInterfaces().toList()
                .filter { it.isUp && !it.isLoopback }
                .sortedBy { if (it.name.startsWith("wlan")) 0 else 1 }
                .flatMap { it.inetAddresses.toList() }
                .filterIsInstance<Inet4Address>()
                .firstOrNull { it.isSiteLocalAddress }
                ?.hostAddress
        } catch (e: Exception) {
            null
        }
    }

    private fun posixTimeZone(): String {
        val offsetMin = TimeZone.getDefault().getOffset(System.currentTimeMillis()) / 60000
        val sign = if (offsetMin > 0) "-" else "+"
        val a = Math.abs(offsetMin)
        return "LOC$sign%02d:%02d".format(a / 60, a % 60)
    }
}
