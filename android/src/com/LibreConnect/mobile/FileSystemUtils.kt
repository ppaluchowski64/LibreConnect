package com.LibreConnect.mobile

import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Canvas
import android.graphics.drawable.Drawable
import android.media.ThumbnailUtils
import android.net.Uri
import android.os.Build
import android.provider.MediaStore
import android.util.DisplayMetrics
import android.util.Size
import android.webkit.MimeTypeMap
import android.widget.Toast
import androidx.core.content.FileProvider
import androidx.core.graphics.createBitmap
import java.io.ByteArrayOutputStream
import java.io.File

object FileSystemUtils {
    @JvmStatic
    fun shareLogs(context: Context) {
        val storageDir = context.getExternalFilesDir(null) ?: context.filesDir
        val logFiles = storageDir
            .walkTopDown()
            .filter { it.isFile && it.extension.equals("log", ignoreCase = true) }
            .toList()

        if (logFiles.isNullOrEmpty()) {
            Toast.makeText(context, "No logs found to export", Toast.LENGTH_SHORT).show()
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

        if (uris.isEmpty()) {
            Toast.makeText(context, "No logs found to export", Toast.LENGTH_SHORT).show()
            return
        }

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
            if (!bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream) || stream.size() == 0) {
                null
            } else {
                stream.toByteArray()
            }
        }
    }

    private fun getFileIcon(context: Context, filePath: String, density: Int): Bitmap? {
        val file = File(filePath)
        if (!file.exists()) return null

        val pm = context.packageManager

        if (filePath.endsWith(".apk", ignoreCase = true)) {
            val pi = pm.getPackageArchiveInfo(filePath, 0)
            if (pi != null) {
                pi.applicationInfo.sourceDir = filePath
                pi.applicationInfo.publicSourceDir = filePath
                val drawable = pm.getResourcesForApplication(pi.applicationInfo)
                    .getDrawableForDensity(pi.applicationInfo.icon, density, null)
                if (drawable != null) return drawableToBitmap(drawable)
            }
            return null
        }

        val extension = file.extension.lowercase()
        val mimeType = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension) ?: "*/*"

        if (mimeType.startsWith("image/") || mimeType.startsWith("video/")) {
            return try {
                val thumbSize = getThumbnailSize(context, density)

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                    val size = Size(thumbSize, thumbSize)
                    if (mimeType.startsWith("image/")) {
                        ThumbnailUtils.createImageThumbnail(file, size, null)
                    } else {
                        ThumbnailUtils.createVideoThumbnail(file, size, null)
                    }
                } else if (mimeType.startsWith("image/")) {
                    getSafeImageThumbnailPreQ(filePath, thumbSize, thumbSize)
                } else {
                    @Suppress("DEPRECATION")
                    val thumbnail = ThumbnailUtils.createVideoThumbnail(filePath, MediaStore.Video.Thumbnails.MINI_KIND)
                    thumbnail?.let { ThumbnailUtils.extractThumbnail(it, thumbSize, thumbSize) }
                }
            } catch (e: Exception) {
                null
            }
        }

        val intent = Intent(Intent.ACTION_VIEW).apply {
            setDataAndType(Uri.fromFile(file), mimeType)
        }

        val resolveInfo = pm.resolveActivity(intent, PackageManager.MATCH_DEFAULT_ONLY)
        if (resolveInfo != null) {
            val appInfo = resolveInfo.activityInfo.applicationInfo
            val drawable = pm.getResourcesForApplication(appInfo)
                .getDrawableForDensity(appInfo.icon, density, null)
            if (drawable != null) return drawableToBitmap(drawable)
        }

        return null
    }

    private fun getThumbnailSize(context: Context, density: Int): Int {
        val targetDensity = if (density > 0) density else context.resources.displayMetrics.densityDpi
        return maxOf(1, (targetDensity * 48) / DisplayMetrics.DENSITY_MEDIUM)
    }

    private fun getSafeImageThumbnailPreQ(filePath: String, reqWidth: Int, reqHeight: Int): Bitmap? {
        val options = BitmapFactory.Options()
        options.inJustDecodeBounds = true
        BitmapFactory.decodeFile(filePath, options)

        var inSampleSize = 1
        if (options.outHeight > reqHeight || options.outWidth > reqWidth) {
            val halfHeight = options.outHeight / 2
            val halfWidth = options.outWidth / 2
            while (halfHeight / inSampleSize >= reqHeight && halfWidth / inSampleSize >= reqWidth) {
                inSampleSize *= 2
            }
        }

        options.inJustDecodeBounds = false
        options.inSampleSize = inSampleSize

        val bitmap = BitmapFactory.decodeFile(filePath, options) ?: return null
        return ThumbnailUtils.extractThumbnail(bitmap, reqWidth, reqHeight)
    }

    private fun drawableToBitmap(drawable: Drawable): Bitmap {
        val bitmap = createBitmap(drawable.intrinsicWidth, drawable.intrinsicHeight)
        val canvas = Canvas(bitmap)
        drawable.setBounds(0, 0, canvas.width, canvas.height)
        drawable.draw(canvas)
        return bitmap
    }
}
