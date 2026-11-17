package org.thecrewreborn.server

import java.io.File
import java.io.InputStream
import java.io.OutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

object SaveTransfer {

    fun export(save: File, out: OutputStream): Int {
        var n = 0
        ZipOutputStream(out).use { zip ->
            save.walkTopDown().filter { it.isFile }.forEach { f ->
                zip.putNextEntry(ZipEntry("save/" + f.relativeTo(save).invariantSeparatorsPath))
                f.inputStream().use { it.copyTo(zip) }
                zip.closeEntry()
                n++
            }
        }
        return n
    }

    fun backupName(): String = "save-" + SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(Date()) + ".zip"

    fun import(home: File, zip: () -> InputStream): File {
        val save = File(home, "save")
        val entries = read(zip)
        if (entries.isEmpty()) throw IllegalArgumentException("the zip holds no save files")
        val backups = File(home, "backups").apply { mkdirs() }
        val backup = File(backups, backupName())
        if (save.exists()) backup.outputStream().use { export(save, it) }
        val staging = File(home, "save.importing")
        staging.deleteRecursively()
        for ((path, data) in entries) {
            val f = File(staging, path)
            f.parentFile?.mkdirs()
            f.writeBytes(data)
        }
        val old = File(home, "save.old")
        old.deleteRecursively()
        if (save.exists() && !save.renameTo(old)) throw IllegalStateException("cannot move $save aside")
        if (!staging.renameTo(save)) {
            old.renameTo(save)
            throw IllegalStateException("cannot move the imported files to $save")
        }
        old.deleteRecursively()
        return backup
    }

    private const val LIMIT = 512L * 1024 * 1024

    private fun read(zip: () -> InputStream): List<Pair<String, ByteArray>> {
        val raw = mutableListOf<Pair<String, ByteArray>>()
        var total = 0L
        ZipInputStream(zip()).use { z ->
            while (true) {
                val e = z.nextEntry ?: break
                val name = e.name.replace('\\', '/')
                if (e.isDirectory || name.startsWith("__MACOSX/") || name.substringAfterLast('/') == ".DS_Store") continue
                if (name.startsWith("/") || name.split('/').any { it == ".." }) {
                    throw IllegalArgumentException("unsafe path in the zip: $name")
                }
                val data = z.readBytes()
                total += data.size
                if (total > LIMIT) throw IllegalArgumentException("the zip is larger than 512 MB")
                raw.add(name to data)
            }
        }
        val withSave = raw.any { it.first.startsWith("save/") }
        return raw.filter { !withSave || it.first.startsWith("save/") }
            .map { (name, data) -> (if (withSave) name.removePrefix("save/") else name) to data }
            .filter { it.first.isNotEmpty() }
    }
}
