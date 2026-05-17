package com.LibreConnect.mobile

import android.app.NotificationChannel
import android.app.Notification
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Build

object RemoteInputNotification {
    private const val CHANNEL_ID = "libreconnect_remote_input"
    private const val NOTIFICATION_ID = 1302
    private const val EXTRA_ACTION = "remote_input_action"
    private const val EXTRA_SEEK_POSITION_SECONDS = "remote_input_seek_position_seconds"

    const val ACTION_PREVIOUS = "previous"
    const val ACTION_PLAY_PAUSE = "play_pause"
    const val ACTION_PLAY = "play"
    const val ACTION_PAUSE = "pause"
    const val ACTION_NEXT = "next"
    const val ACTION_VOLUME_DOWN = "volume_down"
    const val ACTION_VOLUME_UP = "volume_up"

    @Volatile
    private var mediaSession: MediaSession? = null

    @JvmStatic
    fun show(
        context: Context,
        title: String,
        artist: String,
        collection: String,
        elapsed: String,
        playing: Boolean
    ) {
        show(context, title, artist, collection, elapsed, playing, 0.0, 0.0, null)
    }

    @JvmStatic
    fun show(
        context: Context,
        title: String,
        artist: String,
        collection: String,
        elapsed: String,
        playing: Boolean,
        positionSeconds: Double,
        durationSeconds: Double,
        coverBytes: ByteArray?
    ) {
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createChannel(manager)
        val session = getMediaSession(context.applicationContext)
        val cover = decodeCover(coverBytes)
        val safePositionMs = (positionSeconds.coerceAtLeast(0.0) * 1000.0).toLong()
        val safeDurationMs = (durationSeconds.coerceAtLeast(0.0) * 1000.0).toLong()

        updateSession(
            session,
            title,
            artist,
            collection,
            playing,
            safePositionMs,
            safeDurationMs,
            cover
        )

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

        val builder = Notification.Builder(context, CHANNEL_ID)
            .setSmallIcon(android.R.drawable.ic_media_play)
            .setContentTitle(if (title.isNotEmpty()) title else "LibreConnect Remote Input")
            .setContentText(if (subtitle.isNotEmpty()) subtitle else "Control desktop media")
            .setCategory(Notification.CATEGORY_TRANSPORT)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setOngoing(false)
            .setOnlyAlertOnce(true)
            .setShowWhen(false)
            .setStyle(
                Notification.MediaStyle()
                    .setMediaSession(session.sessionToken)
                    .setShowActionsInCompactView(0, 1, 2)
            )
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

        if (cover != null) {
            builder.setLargeIcon(cover)
        }

        if (contentPendingIntent != null) {
            builder.setContentIntent(contentPendingIntent)
        }

        manager.notify(NOTIFICATION_ID, builder.build())
    }

    @JvmStatic
    fun hide(context: Context) {
        val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        manager.cancel(NOTIFICATION_ID)
        mediaSession?.isActive = false
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
    ): Notification.Action {
        val intent = Intent(context, RemoteInputMediaActionReceiver::class.java).apply {
            putExtra(EXTRA_ACTION, action)
        }

        val pendingIntent = PendingIntent.getBroadcast(
            context,
            700 + requestCode,
            intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return Notification.Action.Builder(icon, label, pendingIntent).build()
    }

    internal fun extractAction(intent: Intent?): String? {
        return intent?.getStringExtra(EXTRA_ACTION)
    }

    internal fun extractSeekPositionSeconds(intent: Intent?): Double? {
        if (intent?.hasExtra(EXTRA_SEEK_POSITION_SECONDS) != true) {
            return null
        }

        return intent.getDoubleExtra(EXTRA_SEEK_POSITION_SECONDS, 0.0)
    }

    private fun getMediaSession(context: Context): MediaSession {
        mediaSession?.let { return it }

        synchronized(this) {
            mediaSession?.let { return it }

            val session = MediaSession(context, "LibreConnectDesktopMedia").apply {
                setCallback(object : MediaSession.Callback() {
                    override fun onPlay() = dispatchAction(context, ACTION_PLAY)
                    override fun onPause() = dispatchAction(context, ACTION_PAUSE)
                    override fun onSkipToPrevious() = dispatchAction(context, ACTION_PREVIOUS)
                    override fun onSkipToNext() = dispatchAction(context, ACTION_NEXT)
                    override fun onSeekTo(pos: Long) = dispatchSeek(context, pos)
                    override fun onMediaButtonEvent(mediaButtonIntent: Intent): Boolean {
                        return super.onMediaButtonEvent(mediaButtonIntent)
                    }
                })
            }
            mediaSession = session
            return session
        }
    }

    private fun updateSession(
        session: MediaSession,
        title: String,
        artist: String,
        collection: String,
        playing: Boolean,
        positionMs: Long,
        durationMs: Long,
        cover: Bitmap?
    ) {
        val metadata = MediaMetadata.Builder()
            .putString(MediaMetadata.METADATA_KEY_TITLE, title.ifEmpty { "LibreConnect Remote Input" })
            .putString(MediaMetadata.METADATA_KEY_ARTIST, artist.ifEmpty { "Remote desktop" })
            .putString(MediaMetadata.METADATA_KEY_ALBUM, collection)
            .putLong(MediaMetadata.METADATA_KEY_DURATION, durationMs)
            .apply {
                if (cover != null) {
                    putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, cover)
                    putBitmap(MediaMetadata.METADATA_KEY_ART, cover)
                }
            }
            .build()

        val state = if (playing) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED
        val actions = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
            PlaybackState.ACTION_PLAY_PAUSE or PlaybackState.ACTION_SKIP_TO_PREVIOUS or
            PlaybackState.ACTION_SKIP_TO_NEXT or PlaybackState.ACTION_SEEK_TO

        val playbackState = PlaybackState.Builder()
            .setActions(actions)
            .setState(state, positionMs, if (playing) 1.0f else 0.0f)
            .build()

        session.setMetadata(metadata)
        session.setPlaybackState(playbackState)
        session.isActive = true
    }

    private fun decodeCover(coverBytes: ByteArray?): Bitmap? {
        if (coverBytes == null || coverBytes.isEmpty()) {
            return null
        }

        return BitmapFactory.decodeByteArray(coverBytes, 0, coverBytes.size)
    }

    private fun dispatchAction(context: Context, action: String) {
        val normalizedAction = when (action) {
            ACTION_PLAY, ACTION_PAUSE -> ACTION_PLAY_PAUSE
            else -> action
        }

        val intent = Intent(context, RemoteInputMediaActionReceiver::class.java).apply {
            putExtra(EXTRA_ACTION, normalizedAction)
        }
        context.sendBroadcast(intent)
    }

    private fun dispatchSeek(context: Context, positionMs: Long) {
        val intent = Intent(context, RemoteInputMediaActionReceiver::class.java).apply {
            putExtra(EXTRA_SEEK_POSITION_SECONDS, positionMs.coerceAtLeast(0L) / 1000.0)
        }
        context.sendBroadcast(intent)
    }
}
