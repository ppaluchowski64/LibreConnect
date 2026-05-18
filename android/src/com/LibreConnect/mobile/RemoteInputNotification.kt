package com.LibreConnect.mobile

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Build
import androidx.core.app.NotificationCompat

object RemoteInputNotification {
    private const val CHANNEL_ID = "libreconnect_remote_input"
    private const val NOTIFICATION_ID = 1302
    private const val EXTRA_ACTION = "remote_input_action"

    const val ACTION_PREVIOUS = "previous"
    const val ACTION_PLAY_PAUSE = "play_pause"
    const val ACTION_NEXT = "next"
    const val ACTION_VOLUME_DOWN = "volume_down"
    const val ACTION_VOLUME_UP = "volume_up"

    @JvmStatic
    fun show(
        context: Context,
        title: String,
        artist: String,
        collection: String,
        elapsed: String,
        playing: Boolean
    ) {
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createChannel(manager)

        val subtitle = buildString {
            if (artist.isNotEmpty()) {
                append(artist)
            }
            if (collection.isNotEmpty()) {
                if (isNotEmpty()) append(" | ")
                append(collection)
            }
            if (elapsed.isNotEmpty()) {
                if (isNotEmpty()) append(" | ")
                append(elapsed)
            }
        }

        val launchIntent = context.packageManager.getLaunchIntentForPackage(context.packageName)
        val contentPendingIntent = launchIntent?.let {
            PendingIntent.getActivity(
                context,
                500,
                it,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
        }

        val builder = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(if (title.isNotEmpty()) title else "LibreConnect Remote Input")
            .setContentText(if (subtitle.isNotEmpty()) subtitle else "Control desktop media")
            .setPriority(NotificationCompat.PRIORITY_LOW)
            .setCategory(NotificationCompat.CATEGORY_TRANSPORT)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setOngoing(false)
            .setOnlyAlertOnce(true)
            .addAction(buildAction(context, ACTION_PREVIOUS, "Previous", android.R.drawable.ic_media_previous, 0))
            .addAction(
                buildAction(
                    context,
                    ACTION_PLAY_PAUSE,
                    if (playing) "Pause" else "Play",
                    if (playing) android.R.drawable.ic_media_pause else android.R.drawable.ic_media_play,
                    1
                )
            )
            .addAction(buildAction(context, ACTION_NEXT, "Next", android.R.drawable.ic_media_next, 2))
            .addAction(buildAction(context, ACTION_VOLUME_DOWN, "Volume -", android.R.drawable.ic_lock_silent_mode, 3))
            .addAction(buildAction(context, ACTION_VOLUME_UP, "Volume +", android.R.drawable.ic_lock_silent_mode_off, 4))

        if (contentPendingIntent != null) {
            builder.setContentIntent(contentPendingIntent)
        }

        manager.notify(NOTIFICATION_ID, builder.build())
    }

    @JvmStatic
    fun hide(context: Context) {
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.cancel(NOTIFICATION_ID)
    }

    private fun createChannel(manager: NotificationManager) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Remote Input",
                NotificationManager.IMPORTANCE_LOW
            )
            manager.createNotificationChannel(channel)
        }
    }

    private fun buildAction(
        context: Context,
        action: String,
        label: String,
        icon: Int,
        requestCode: Int
    ): NotificationCompat.Action {
        val intent = Intent(context, RemoteInputMediaActionReceiver::class.java).apply {
            putExtra(EXTRA_ACTION, action)
        }

        val pendingIntent = PendingIntent.getBroadcast(
            context,
            700 + requestCode,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Action.Builder(icon, label, pendingIntent).build()
    }

    internal fun extractAction(intent: Intent?): String? {
        return intent?.getStringExtra(EXTRA_ACTION)
    }
}
