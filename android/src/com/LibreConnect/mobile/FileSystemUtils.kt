package com.LibreConnect.mobile

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.net.Uri
import java.io.File
import androidx.core.graphics.createBitmap
import java.io.ByteArrayOutputStream
import androidx.core.content.FileProvider

object FileSystemUtils {
    @JvmStatic
    fun shareLogs(context: Context) {
        val filesDir = context.filesDir
        val logFiles = filesDir.listFiles { _, name -> name.endsWith(".log") }
        
        if (logFiles.isNullOrEmpty()) {
            return
        }

        val uris = ArrayList<Uri>()
        for (file in logFiles) {
            try {
                val contentUri = FileProvider.getUriForFile(
                    context,
                    "com.LibreConnect.mobile.fileprovider",
                    file
                )
                uris.add(contentUri)
            } catch (e: Exception) {
                // Skip if couldn't get URI
            }
        }

        if (uris.isEmpty()) return

        val intent = Intent(Intent.ACTION_SEND_MULTIPLE).apply {
            type = "text/plain"
            putParcelableArrayListExtra(Intent.EXTRA_STREAM, uris)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }

        val chooser = Intent.createChooser(intent, "Export Logs")
        chooser.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        context.startActivity(chooser)
    }

    @JvmStatic
    fun getFileIconAsPngBytes(context: Context, filePath: String, density: Int): ByteArray? {
        val bitmap = getFileIcon(context, filePath, density) ?: return null

        return ByteArrayOutputStream().use { stream ->
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
            stream.toByteArray()
        }
    }

    private fun getFileIcon(context: Context, filePath: String, density: Int): Bitmap? {
        val pm = context.packageManager
        var drawable: Drawable? = null

        if (filePath.endsWith(".apk")) {
            val pi = pm.getPackageArchiveInfo(filePath, 0)
            if (pi != null) {
                pi.applicationInfo.sourceDir = filePath
                pi.applicationInfo.publicSourceDir = filePath
                drawable = pm.getResourcesForApplication(pi.applicationInfo).getDrawableForDensity(pi.applicationInfo.icon, density, null)
            }
        } else {
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(Uri.fromFile(File(filePath)), "*/*")
            }

            val resolveInfo = pm.resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY)

            if (resolveInfo != null) {
                val appInfo = resolveInfo.activityInfo.applicationInfo
                drawable = pm.getResourcesForApplication(appInfo).getDrawableForDensity(appInfo.icon, density, null)
            }
        }

        if (drawable == null) return null

        return drawableToBitmap(drawable)
    }

    private fun drawableToBitmap(drawable: Drawable): Bitmap {
        val bitmap = createBitmap(drawable.intrinsicWidth, drawable.intrinsicHeight)
        val canvas = Canvas(bitmap)
        drawable.setBounds(0, 0, canvas.width, canvas.height)
        drawable.draw(canvas)
        return bitmap
    }
}
