package org.thecrewreborn.server

import android.content.Context
import android.os.Process as AndroidProcess
import java.io.File

class ServerFiles(private val context: Context) {

    companion object {
        private const val PREFS = "app"
        private const val KEY_HOME = "server_home"

        fun appHome(context: Context) = File(context.filesDir, "server")

        fun chosenHome(context: Context): File? =
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getString(KEY_HOME, null)?.let { File(it) }

        fun setChosenHome(context: Context, folder: File?) {
            context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
                .putString(KEY_HOME, folder?.absolutePath).apply()
        }
    }

    val home = chosenHome(context) ?: appHome(context)
    val logFile = File(context.filesDir, "logs/server.log")
    private val config = File(home, "config.ini")
    private val pidFile = File(context.filesDir, "logs/server.pid")
    val save = File(home, "save")

    fun prepare() {
        logFile.parentFile?.mkdirs()
        home.mkdirs()
        if (config.exists()) return
        val example = context.assets.open("config.example.ini").bufferedReader().use { it.readText() }
        config.writeText(firstConfig(example))
    }

    private fun firstConfig(example: String): String {
        var section = ""
        return example.lines().joinToString("\n") { line ->
            val t = line.trim()
            if (t.startsWith("[") && t.endsWith("]")) section = t
            when {
                section == "[server]" && t.startsWith("http_port =") -> "http_port = 8080"
                section == "[server]" && t.startsWith("https_port =") -> "https_port = 8443"
                section == "[ui]" && t.startsWith("open =") -> "open = off"
                else -> line
            }
        }
    }

    private fun value(section: String, key: String): String? {
        if (!config.exists()) return null
        var current = ""
        for (line in config.readLines()) {
            val t = line.trim()
            if (t.startsWith(";") || t.startsWith("#")) continue
            if (t.startsWith("[") && t.endsWith("]")) {
                current = t
                continue
            }
            val eq = t.indexOf('=')
            if (current == section && eq > 0 && t.substring(0, eq).trim() == key) return t.substring(eq + 1).trim()
        }
        return null
    }

    fun adminPort(): Int = value("[ui]", "port")?.toIntOrNull()?.takeIf { it > 0 } ?: 8090

    fun httpsPort(): Int = value("[server]", "https_port")?.toIntOrNull() ?: 8443

    fun pid(): Int? = pidFile.takeIf { it.exists() }?.readText()?.trim()?.toIntOrNull()

    fun startCommand(exe: File): List<String> =
        listOf("/system/bin/sh", "-c", "echo \$\$ > \"\$1\" && exec \"\$2\"", "tcreborn", pidFile.absolutePath, exe.absolutePath)

    fun clearPid() {
        pidFile.delete()
    }

    fun killStale() {
        val pid = pid() ?: return
        val cmdline = File("/proc/$pid/cmdline")
        if (cmdline.exists() && cmdline.readText().contains("libtcreborn.so")) {
            AndroidProcess.sendSignal(pid, 15)
            for (i in 0 until 50) {
                if (!cmdline.exists()) break
                Thread.sleep(100)
            }
            if (cmdline.exists()) AndroidProcess.killProcess(pid)
        }
        pidFile.delete()
    }
}
