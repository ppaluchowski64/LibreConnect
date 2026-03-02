package com.LibreConnect.mobile
import android.app.Notification
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.io.ByteArrayOutputStream
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale

class NotificationListener : NotificationListenerService() {
    private val activeNotifications = mutableListOf<String>()
    external fun onNotificationReceivedCPP(key: String, title: String?, content: String?, timestamp: Long, largeIconBytes: ByteArray?, mainImage: ByteArray?)
    external fun onNotificationRemovedCPP(key: String)

    companion object {
        init {
            System.loadLibrary("LibreConnectNative")
        }
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val key: String = sbn.key
        activeNotifications.add(key);

        val notification = sbn.notification
        val extras = notification.extras

        val title: String? = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()
        val content: String? = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()
        val timestamp: Long = sbn.postTime
        val largeIcon = notification.getLargeIcon()
        var mainImageBytes: ByteArray? = null

        val bigPicture = if (android.os.Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            extras.getParcelable(Notification.EXTRA_PICTURE_ICON, android.graphics.drawable.Icon::class.java)
        } else {
            extras.getParcelable(Notification.EXTRA_PICTURE) as? Bitmap
        }

        if (bigPicture != null) {
            val bitmap = if (bigPicture is android.graphics.drawable.Icon) {
                drawableToBitmap(bigPicture.loadDrawable(applicationContext)!!)
            } else {
                bigPicture as Bitmap
            }

            if (bitmap.width > 1024) {
                val aspectRatio = bitmap.height.toDouble() / bitmap.width.toDouble()
                val scaledBitmap = bitmap.scale(1024, (1024 * aspectRatio).toInt())
                mainImageBytes = bitmapToByteArray(scaledBitmap)
            }

            mainImageBytes = bitmapToByteArray(bitmap)
        }

        val iconBytes: ByteArray? = if (largeIcon != null) {
            val drawable = largeIcon.loadDrawable(applicationContext)
            drawable?.let {
                val bitmap = drawableToBitmap(it)
                bitmapToByteArray(bitmap)
            }

        } else {
            val smallIconDrawable = notification.smallIcon?.loadDrawable(applicationContext)
            smallIconDrawable?.let {
                val bitmap = drawableToBitmap(it)
                bitmapToByteArray(bitmap)
            }
        }

        onNotificationReceivedCPP(
            key,
            title,
            content,
            timestamp,
            iconBytes,
            mainImageBytes
        )
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        val key = sbn.key
        activeNotifications.remove(key);

        onNotificationRemovedCPP(
            key
        )
    }

    private fun drawableToBitmap(drawable: Drawable): Bitmap {
        if (drawable is BitmapDrawable) {
            return drawable.bitmap
        }

        val width = drawable.intrinsicWidth.takeIf { it > 0 } ?: 1
        val height = drawable.intrinsicHeight.takeIf { it > 0 } ?: 1

        val bitmap = createBitmap(width, height)
        val canvas = Canvas(bitmap)
        drawable.setBounds(0, 0, canvas.width, canvas.height)
        drawable.draw(canvas)

        return bitmap
    }

    private fun bitmapToByteArray(bitmap: Bitmap): ByteArray {
        val stream = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        return stream.toByteArray()
    }
}