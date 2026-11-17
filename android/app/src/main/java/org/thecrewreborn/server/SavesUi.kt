package org.thecrewreborn.server

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.provider.DocumentsContract
import android.provider.Settings
import android.widget.Toast
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class SavesUi(private val activity: Activity, private val afterRestart: () -> Unit) {

    companion object {
        const val REQUEST_EXPORT = 10
        const val REQUEST_IMPORT = 11
        const val REQUEST_FOLDER = 12
        const val REQUEST_STORAGE_PERMISSION = 13
    }

    private val main = Handler(Looper.getMainLooper())
    private var folderAfterPermission = false

    private fun toast(text: String) = Toast.makeText(activity, text, Toast.LENGTH_LONG).show()
    private fun s(id: Int, vararg args: Any) = activity.getString(id, *args)

    fun export() {
        val name = "TheCrewReborn-save-" + SimpleDateFormat("yyyyMMdd-HHmm", Locale.ROOT).format(Date()) + ".zip"
        val pick = Intent(Intent.ACTION_CREATE_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("application/zip")
            .putExtra(Intent.EXTRA_TITLE, name)
        activity.startActivityForResult(pick, REQUEST_EXPORT)
    }

    private fun exportTo(uri: Uri) {
        val save = ServerFiles(activity).save
        Thread({
            val result = try {
                val n = activity.contentResolver.openOutputStream(uri)!!.use { SaveTransfer.export(save, it) }
                s(R.string.export_done, n)
            } catch (e: Exception) {
                s(R.string.export_failed, e.message ?: e.toString())
            }
            main.post { toast(result) }
        }, "export").start()
    }

    fun import() {
        val pick = Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, arrayOf("application/zip", "application/x-zip-compressed",
                "application/octet-stream"))
            .putExtra("android.content.extra.SHOW_ADVANCED", true)
            .putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:Download"))
        activity.startActivityForResult(pick, REQUEST_IMPORT)
    }

    private fun importFrom(uri: Uri) {
        AlertDialog.Builder(activity)
            .setTitle(R.string.import_title)
            .setMessage(R.string.import_confirm)
            .setPositiveButton(R.string.import_go) { _, _ ->
                withServerStopped { home ->
                    val backup = SaveTransfer.import(home) { activity.contentResolver.openInputStream(uri)!! }
                    s(R.string.import_done, "backups/" + backup.name)
                }
            }
            .setNegativeButton(android.R.string.cancel, null)
            .show()
    }

    private fun withServerStopped(work: (File) -> String) {
        Thread({
            if (ServerService.running) {
                main.post { ServerService.stop(activity) }
                var waited = 0
                while (ServerService.running && waited < 15000) {
                    Thread.sleep(200)
                    waited += 200
                }
            }
            val result = if (ServerService.running) {
                s(R.string.still_running)
            } else {
                try {
                    work(ServerFiles(activity).home)
                } catch (e: Exception) {
                    s(R.string.import_failed, e.message ?: e.toString())
                }
            }
            main.post {
                toast(result)
                ServerService.start(activity)
                afterRestart()
            }
        }, "saves").start()
    }

    fun chooseFolder() {
        val current = ServerFiles(activity).home.absolutePath
        val dialog = AlertDialog.Builder(activity)
            .setTitle(R.string.folder_title)
            .setMessage(s(R.string.folder_message, current))
            .setPositiveButton(R.string.folder_pick) { _, _ -> pickFolder() }
            .setNegativeButton(android.R.string.cancel, null)
        if (ServerFiles.chosenHome(activity) != null) {
            dialog.setNeutralButton(R.string.folder_app) { _, _ -> switchTo(null) }
        }
        dialog.show()
    }

    private fun hasStorageAccess(): Boolean = if (Build.VERSION.SDK_INT >= 30) {
        Environment.isExternalStorageManager()
    } else {
        activity.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
    }

    private fun folderPicker(): Intent {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE).putExtra("android.content.extra.SHOW_ADVANCED", true)
        val start = ServerFiles.chosenHome(activity)?.let { File(it.parentFile ?: it, "") }
        val rel = start?.absolutePath?.removePrefix(Environment.getExternalStorageDirectory().absolutePath + "/")
        val doc = if (rel != null && rel != start.absolutePath) "primary:$rel" else "primary:"
        i.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
            DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", doc))
        return i
    }

    private fun pickFolder() {
        if (hasStorageAccess()) {
            activity.startActivityForResult(folderPicker(), REQUEST_FOLDER)
            return
        }
        folderAfterPermission = true
        if (Build.VERSION.SDK_INT >= 30) {
            AlertDialog.Builder(activity)
                .setMessage(R.string.folder_permission)
                .setPositiveButton(android.R.string.ok) { _, _ ->
                    try {
                        activity.startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                            Uri.parse("package:" + activity.packageName)))
                    } catch (e: Exception) {
                        activity.startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                    }
                }
                .setNegativeButton(android.R.string.cancel) { _, _ -> folderAfterPermission = false }
                .show()
        } else {
            activity.requestPermissions(arrayOf(Manifest.permission.WRITE_EXTERNAL_STORAGE),
                REQUEST_STORAGE_PERMISSION)
        }
    }

    fun onResume() {
        if (folderAfterPermission && hasStorageAccess()) {
            folderAfterPermission = false
            activity.startActivityForResult(folderPicker(), REQUEST_FOLDER)
        }
    }

    fun onRequestPermissionsResult(requestCode: Int) {
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            if (hasStorageAccess()) onResume() else {
                folderAfterPermission = false
                toast(s(R.string.folder_no_permission))
            }
        }
    }

    private fun pathOf(tree: Uri): File? {
        if (tree.authority != "com.android.externalstorage.documents") return null
        val id = DocumentsContract.getTreeDocumentId(tree)
        val volume = id.substringBefore(':')
        val rel = id.substringAfter(':', "")
        val root = when (volume) {
            "primary" -> Environment.getExternalStorageDirectory()
            "home" -> Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS)
            else -> File("/storage/$volume")
        }
        return if (rel.isEmpty()) root else File(root, rel)
    }

    private fun switchTo(folder: File?) {
        val target = folder ?: ServerFiles.appHome(activity)
        withServerStopped { from ->
            target.mkdirs()
            val probe = File(target, ".tcreborn-write-test")
            try {
                probe.writeText("ok")
            } finally {
                probe.delete()
            }
            val copied = if (!File(target, "config.ini").exists() && !File(target, "save").exists()) {
                for (name in listOf("config.ini", "save", "certs")) {
                    val src = File(from, name)
                    if (src.exists()) src.copyRecursively(File(target, name), overwrite = false)
                }
                true
            } else {
                false
            }
            ServerFiles.setChosenHome(activity, folder)
            s(if (copied) R.string.folder_copied else R.string.folder_used, target.absolutePath)
        }
    }

    fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        val uri = data?.data
        when (requestCode) {
            REQUEST_EXPORT -> if (resultCode == Activity.RESULT_OK && uri != null) exportTo(uri)
            REQUEST_IMPORT -> if (resultCode == Activity.RESULT_OK && uri != null) importFrom(uri)
            REQUEST_FOLDER -> if (resultCode == Activity.RESULT_OK && uri != null) {
                val path = pathOf(uri)
                if (path == null) toast(s(R.string.folder_unsupported)) else {
                    AlertDialog.Builder(activity)
                        .setTitle(R.string.folder_title)
                        .setMessage(s(R.string.folder_confirm, path.absolutePath))
                        .setPositiveButton(android.R.string.ok) { _, _ -> switchTo(path) }
                        .setNegativeButton(android.R.string.cancel, null)
                        .show()
                }
            }
            else -> return false
        }
        return true
    }
}
