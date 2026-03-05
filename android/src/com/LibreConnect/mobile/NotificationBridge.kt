package com.LibreConnect.mobile
import android.app.PendingIntent
import android.app.NotificationChannel
import android.app.NotificationManager
import android.content.Context
import android.content.Intent
import android.graphics.BitmapFactory
import android.os.Build
import androidx.annotation.Keep
import androidx.core.graphics.drawable.IconCompat
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
        smallIcon: ByteArray?,
        largeIcon: ByteArray?
    ) {
        postNotification(key, title, content, timestamp, smallIcon, largeIcon, null)
    }

    @Keep
    fun postNotification(
        key: String,
        title: String?,
        content: String?,
        timestamp: Long,
        smallIcon: ByteArray?,
        largeIcon: ByteArray?,
        buttons: Array<String>?

    ) {

        val builder = NotificationCompat.Builder(context, CHANNEL_ID)
            .setContentTitle(title)
            .setContentText(content)
            .setWhen(timestamp)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setPriority(NotificationCompat.PRIORITY_DEFAULT)

        if (buttons != null) {
            for ((index, button) in buttons.withIndex()) {
                val intent = Intent(context, NotificationActionReceiver::class.java).apply {
                    putExtra("notification_key", key)
                    putExtra("notification_option", button)
                }

                val requestCode = 31 * key.hashCode() + index
                val pendingIntent = PendingIntent.getBroadcast(
                    context,
                    requestCode,
                    intent,
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
                )

                builder.addAction(
                    android.R.drawable.ic_menu_send,
                    button,
                    pendingIntent
                )
            }
        }


        if (smallIcon != null && smallIcon.isNotEmpty()) {
            val bitmap = BitmapFactory.decodeByteArray(smallIcon, 0, smallIcon.size)
            if (bitmap != null) {
                val iconCompat = IconCompat.createWithBitmap(bitmap)
                builder.setSmallIcon(iconCompat)
            }
        }

        if (largeIcon != null && largeIcon.isNotEmpty()) {
            val bitmap = BitmapFactory.decodeByteArray(largeIcon, 0, largeIcon.size)
            if (bitmap != null) {
                builder.setLargeIcon(bitmap)
            }
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
