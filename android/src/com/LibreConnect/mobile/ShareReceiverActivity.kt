package com.LibreConnect.mobile

import android.app.Activity
import android.content.Intent
import android.database.Cursor
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.OpenableColumns
import android.util.Log
import android.widget.Toast
import java.io.File

class ShareReceiverActivity : Activity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        handleShareIntent(intent)
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        if (intent != null) {
            handleShareIntent(intent)
        }
    }

    private fun handleShareIntent(intent: Intent) {
        Thread {
            val sharedFiles = copySharedFiles(intent)
            if (sharedFiles.isEmpty()) {
                showToast("No shared files found")
                finishShareActivity()
                return@Thread
            }

            var queuedCount = 0
            for (file in sharedFiles) {
                if (SharedFileDispatcher.postSharedFile(this, file.absolutePath)) {
                    ++queuedCount
                }
            }

            showToast(shareResultMessage(queuedCount))
            finishShareActivity()
        }.start()
    }

    private fun copySharedFiles(intent: Intent): List<File> {
        val uris = when (intent.action) {
            Intent.ACTION_SEND -> {
                val uri = getStreamUri(intent)
                if (uri == null) emptyList() else listOf(uri)
            }
            Intent.ACTION_SEND_MULTIPLE -> intent.getParcelableArrayListExtra<Uri>(Intent.EXTRA_STREAM)
                ?: emptyList()
            else -> emptyList()
        }

        if (uris.isEmpty()) {
            return emptyList()
        }

        val targetDir = File(cacheDir, "shared-files").apply {
            mkdirs()
        }

        return uris.mapNotNull { uri ->
            try {
                copyUriToFile(uri, targetDir)
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to copy shared URI: $uri", t)
                null
            }
        }
    }

    @Suppress("DEPRECATION")
    private fun getStreamUri(intent: Intent): Uri? {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri::class.java)
        } else {
            intent.getParcelableExtra(Intent.EXTRA_STREAM)
        }
    }

    private fun copyUriToFile(uri: Uri, targetDir: File): File? {
        val displayName = queryDisplayName(uri) ?: uri.lastPathSegment ?: "shared-file"
        val targetFile = uniqueFile(targetDir, sanitizeFileName(displayName))

        contentResolver.openInputStream(uri)?.use { input ->
            targetFile.outputStream().use { output ->
                input.copyTo(output)
            }
        } ?: return null

        return targetFile
    }

    private fun queryDisplayName(uri: Uri): String? {
        var cursor: Cursor? = null
        return try {
            cursor = contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            if (cursor != null && cursor.moveToFirst()) {
                val index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                if (index >= 0) cursor.getString(index) else null
            } else {
                null
            }
        } catch (_: Throwable) {
            null
        } finally {
            cursor?.close()
        }
    }

    private fun uniqueFile(directory: File, requestedName: String): File {
        val safeName = requestedName.ifBlank { "shared-file" }
        val dotIndex = safeName.lastIndexOf('.')
        val baseName = if (dotIndex > 0) safeName.substring(0, dotIndex) else safeName
        val extension = if (dotIndex > 0) safeName.substring(dotIndex) else ""

        var candidate = File(directory, safeName)
        var index = 1
        while (candidate.exists()) {
            candidate = File(directory, "$baseName ($index)$extension")
            ++index
        }

        return candidate
    }

    private fun sanitizeFileName(name: String): String {
        return name.replace(Regex("""[\\/:*?"<>|\x00-\x1F]"""), "_").trim().ifBlank { "shared-file" }
    }

    private fun showToast(message: String) {
        runOnUiThread {
            Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
        }
    }

    private fun shareResultMessage(queuedCount: Int): String {
        return when (queuedCount) {
            0 -> "Could not send shared files to desktop"
            1 -> "Sending shared file to desktop"
            else -> "Sending $queuedCount shared files to desktop"
        }
    }

    private fun finishShareActivity() {
        runOnUiThread {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                finishAndRemoveTask()
            } else {
                finish()
            }
        }
    }

    companion object {
        private const val TAG = "ShareReceiverActivity"
    }
}
