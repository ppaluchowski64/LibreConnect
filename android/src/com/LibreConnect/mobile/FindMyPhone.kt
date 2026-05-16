package com.LibreConnect.mobile

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.database.Cursor
import android.media.AudioAttributes
import android.media.AudioManager
import android.media.MediaPlayer
import android.media.RingtoneManager
import android.net.Uri
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.os.PowerManager
import android.os.VibrationEffect
import android.os.Vibrator
import android.util.Log
import androidx.annotation.Keep
import androidx.core.app.NotificationCompat
import org.json.JSONArray
import org.json.JSONObject

@Keep
object FindMyPhone {
    private const val TAG = "FindMyPhone"
    private const val CHANNEL_ID = "LibreConnect.FindMyPhone"
    private const val NOTIFICATION_ID = 9999

    private val alertThread: HandlerThread by lazy {
        HandlerThread("LibreConnectFindMyPhone").apply { start() }
    }
    private val alertHandler: Handler by lazy {
        Handler(alertThread.looper)
    }

    private var mediaPlayer: MediaPlayer? = null
    private var vibrator: Vibrator? = null
    private var originalVolume: Int = -1

    private fun createNotificationChannel(context: Context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            val channel = NotificationChannel(
                CHANNEL_ID,
                "Find My Phone",
                NotificationManager.IMPORTANCE_HIGH
            ).apply {
                description = "High priority notification for Find My Phone feature"
                enableLights(true)
                lightColor = android.graphics.Color.RED
                enableVibration(true)
                lockscreenVisibility = NotificationCompat.VISIBILITY_PUBLIC
            }
            notificationManager.createNotificationChannel(channel)
        }
    }

    private fun showNotification(context: Context) {
        createNotificationChannel(context)

        val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

        val launchIntent = context.packageManager.getLaunchIntentForPackage(context.packageName)
        val contentPendingIntent = PendingIntent.getActivity(
            context,
            0,
            launchIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val stopIntent = Intent(context, NotificationActionReceiver::class.java).apply {
            putExtra("notification_key", "find_my_phone")
            putExtra("notification_option", "Stop")
        }
        val stopPendingIntent = PendingIntent.getBroadcast(
            context,
            NOTIFICATION_ID,
            stopIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        val builder = NotificationCompat.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle("Find My Phone")
            .setContentText("Your phone is ringing!")
            .setPriority(NotificationCompat.PRIORITY_MAX)
            .setCategory(NotificationCompat.CATEGORY_ALARM)
            .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
            .setOngoing(true)
            .setAutoCancel(false)
            .setContentIntent(contentPendingIntent)
            .setFullScreenIntent(contentPendingIntent, true)
            .addAction(android.R.drawable.ic_menu_close_clear_cancel, "Stop", stopPendingIntent)

        notificationManager.notify(NOTIFICATION_ID, builder.build())
    }

    private fun cancelNotification(context: Context) {
        val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        notificationManager.cancel(NOTIFICATION_ID)
    }

    private fun runOnAlertThread(action: () -> Unit) {
        alertHandler.post {
            try {
                action()
            } catch (t: Throwable) {
                Log.e(TAG, "Find My Phone alert operation failed", t)
            }
        }
    }

    private fun appendRingtones(result: JSONArray, context: Context, type: Int, seen: MutableSet<String>) {
        val manager = RingtoneManager(context).apply { setType(type) }
        val cursor: Cursor = manager.cursor ?: return
        cursor.use {
            while (it.moveToNext()) {
                val position = it.position
                val uri = manager.getRingtoneUri(position)?.toString() ?: continue
                if (!seen.add(uri)) {
                    continue
                }

                val title = it.getString(RingtoneManager.TITLE_COLUMN_INDEX) ?: continue
                val item = JSONObject()
                item.put("label", title)
                item.put("uri", uri)
                result.put(item)
            }
        }
    }

    private fun createPlayer(context: Context, uri: Uri): MediaPlayer? {
        var player: MediaPlayer? = null
        return try {
            MediaPlayer().apply {
                player = this
                setDataSource(context, uri)
                setWakeMode(context.applicationContext, PowerManager.PARTIAL_WAKE_LOCK)
                setAudioAttributes(
                    AudioAttributes.Builder()
                        .setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                        .build()
                )
                isLooping = true
                prepare()
                start()
            }
        } catch (t: Throwable) {
            player?.release()
            Log.w(TAG, "Failed to initialize ringtone $uri", t)
            null
        }
    }

    @JvmStatic
    @Keep
    fun getAvailableRingtones(context: Context): String {
        return try {
            val result = JSONArray()
            val seenUris = hashSetOf<String>()

            appendRingtones(result, context, RingtoneManager.TYPE_ALARM, seenUris)
            appendRingtones(result, context, RingtoneManager.TYPE_RINGTONE, seenUris)

            result.toString()
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to enumerate ringtones", t)
            "[]"
        }
    }

    @JvmStatic
    @Keep
    fun getRingtoneTitle(context: Context, ringtoneUri: String?): String {
        if (ringtoneUri.isNullOrBlank()) {
            return ""
        }

        return try {
            val ringtone = RingtoneManager.getRingtone(context, Uri.parse(ringtoneUri))
            ringtone?.getTitle(context).orEmpty()
        } catch (t: Throwable) {
            Log.w(TAG, "Failed to resolve ringtone title", t)
            ""
        }
    }

    @JvmStatic
    @Keep
    fun startAlert(context: Context, customRingtoneUri: String?) {
        val appContext = context.applicationContext
        runOnAlertThread {
            startAlertInternal(appContext, customRingtoneUri)
        }
    }

    private fun startAlertInternal(context: Context, customRingtoneUri: String?) {
        try {
            val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager

            if (originalVolume == -1) {
                originalVolume = audioManager.getStreamVolume(AudioManager.STREAM_ALARM)
            }

            val maxVolume = audioManager.getStreamMaxVolume(AudioManager.STREAM_ALARM)
            audioManager.setStreamVolume(AudioManager.STREAM_ALARM, maxVolume, 0)

            stopPlaybackInternal()

            val candidates = mutableListOf<Uri>()
            if (!customRingtoneUri.isNullOrBlank()) {
                candidates.add(Uri.parse(customRingtoneUri))
            }

            val defaultAlarm = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM)
            val defaultRingtone = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
            val defaultNotification = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
            if (defaultAlarm != null) {
                candidates.add(defaultAlarm)
            }
            if (defaultRingtone != null) {
                candidates.add(defaultRingtone)
            }
            if (defaultNotification != null) {
                candidates.add(defaultNotification)
            }

            for (candidate in candidates) {
                val player = createPlayer(context, candidate)
                if (player != null) {
                    mediaPlayer = player
                    break
                }
            }

            if (mediaPlayer == null) {
                Log.e(TAG, "No playable ringtone found for alert")
                stopAlertInternal(context)
                return
            }

            showNotification(context)

            vibrator = context.getSystemService(Context.VIBRATOR_SERVICE) as Vibrator
            val pattern = longArrayOf(0, 500, 500)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val effect = VibrationEffect.createWaveform(pattern, 0)
                vibrator?.vibrate(effect)
            } else {
                @Suppress("DEPRECATION")
                vibrator?.vibrate(pattern, 0)
            }

        } catch (t: Throwable) {
            Log.e(TAG, "Failed to start alert", t)
            stopAlertInternal(context)
        }
    }

    private fun stopPlaybackInternal() {
        try {
            mediaPlayer?.stop()
        } catch (_: Throwable) {
        }
        mediaPlayer?.release()
        mediaPlayer = null

        vibrator?.cancel()
        vibrator = null
    }

    private fun stopAlertInternal(context: Context) {
        stopPlaybackInternal()
        cancelNotification(context)

        if (originalVolume != -1) {
            val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            audioManager.setStreamVolume(AudioManager.STREAM_ALARM, originalVolume, 0)
            originalVolume = -1
        }
    }

    @JvmStatic
    @Keep
    fun stopAlert(context: Context) {
        val appContext = context.applicationContext
        runOnAlertThread {
            try {
                stopAlertInternal(appContext)
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to stop alert", t)
            }
        }
    }
}
