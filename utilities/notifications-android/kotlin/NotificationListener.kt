package com.LibreConnect.mobile
import android.app.Notification
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import java.io.ByteArrayOutputStream
import androidx.core.graphics.createBitmap

class NotificationListener : NotificationListenerService() {
    private val activeNotifications = mutableListOf<String>()
    external fun onNotificationReceivedCPP(key: String, title: String?, content: String?, timestamp: Long, iconBytes: ByteArray?)
    external fun onNotificationRemovedCPP(key: String)

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        val key = sbn.key
        activeNotifications.add(key);

        val notification = sbn.notification
        val extras = notification.extras

        val title: String? = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()
        val content: String? = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()
        val timestamp: Long = sbn.postTime

        val drawable = notification.smallIcon?.loadDrawable(applicationContext)

        val iconBytes = drawable?.let {
            val bitmap = drawableToBitmap(it)
            bitmapToByteArray(bitmap)
        }

        onNotificationReceivedCPP(
            key,
            title,
            content,
            timestamp,
            iconBytes
        )
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        val key = sbn.key
        activeNotifications.remove(key);


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