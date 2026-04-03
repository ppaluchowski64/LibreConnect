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

object FileSystemUtils {
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
