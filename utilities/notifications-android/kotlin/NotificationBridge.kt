package com.LibreConnect.mobile
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.graphics.BitmapFactory
import android.graphics.drawable.Icon
import android.os.Build
import androidx.annotation.Keep
import androidx.core.app.NotificationCompat

@Keep
class NotificationBridge(private val context: Context) {

    private val notificationManager =
        context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

    init {
        createNotificationChannel()
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "LibreConnect",
                NotificationManager.IMPORTANCE_DEFAULT
            )
            notificationManager.createNotificationChannel(channel)
        }
    }

    @Keep
    fun postNotification(
        key: String,
        title: String?,
        content: String?,
        timestamp: Long,
        iconBytes: ByteArray?
    ) {
        val builder = NotificationCompat.Builder(context, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(content)
            .setWhen(timestamp)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)

        if (iconBytes != null && iconBytes.isNotEmpty()) {
            val bitmap = BitmapFactory.decodeByteArray(iconBytes, 0, iconBytes.size)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                val icon = Icon.createWithBitmap(bitmap)
                builder.setSmallIcon(icon)
            } else {
                builder.setLargeIcon(bitmap)
                // builder.setSmallIcon(R.drawable.your_default_small_icon) // Fallback for API < 23
            }
        } else {
            // Default icon
        }

        notificationManager.notify(key, key.hashCode(), builder.build())
    }

    @Keep
    fun removeNotification(key: String) {
        notificationManager.cancel(key, key.hashCode())
    }

    companion object {
        private const val CHANNEL_ID = "LibreConnect.mobile"
    }
}