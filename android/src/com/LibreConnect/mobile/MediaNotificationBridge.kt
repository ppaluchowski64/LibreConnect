package com.LibreConnect.mobile

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.media.MediaMetadata
import android.media.session.MediaSession
import android.media.session.PlaybackState
import android.os.Handler
import android.os.Looper

class MediaNotificationBridge {
    companion object {
        private const val CHANNEL_ID = "libreconnect_remote_input"
        private const val NOTIFICATION_ID = 1302

        @Volatile
        var mediaSession: MediaSession? = null

        @JvmStatic
        fun show(context: Context) {
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            createChannel(manager)
            getMediaSession(context.applicationContext)
        }

        @JvmStatic
        fun hide(context: Context) {
            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.cancel(NOTIFICATION_ID)
            mediaSession?.isActive = false
            mediaSession?.release()
            mediaSession = null
        }

        @JvmStatic
        fun updateMetadata(context: Context, title: String, artist: String, album: String, durationMicros: Long, coverBytes: ByteArray?) {
            val session = getMediaSession(context.applicationContext)
            val cover = decodeCover(coverBytes)

            val metadata = MediaMetadata.Builder()
                .putString(MediaMetadata.METADATA_KEY_TITLE, title.ifEmpty { "Unknown track" })
                .putString(MediaMetadata.METADATA_KEY_ARTIST, artist.ifEmpty { "LibreConnect" })
                .putString(MediaMetadata.METADATA_KEY_ALBUM, album)
                .putLong(MediaMetadata.METADATA_KEY_DURATION, durationMicros / 1000L)
                .apply {
                    if (cover != null) {
                        putBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART, cover)
                        putBitmap(MediaMetadata.METADATA_KEY_ART, cover)
                    }
                }
                .build()

            session.setMetadata(metadata)
            updateNotification(context, title, artist, cover)
        }

        @JvmStatic
        fun updatePlaybackState(context: Context, isPlaying: Boolean, positionMicros: Long) {
            val session = getMediaSession(context.applicationContext)
            val state = if (isPlaying) PlaybackState.STATE_PLAYING else PlaybackState.STATE_PAUSED
            val actions = PlaybackState.ACTION_PLAY or PlaybackState.ACTION_PAUSE or
                    PlaybackState.ACTION_PLAY_PAUSE or PlaybackState.ACTION_SKIP_TO_PREVIOUS or
                    PlaybackState.ACTION_SKIP_TO_NEXT or PlaybackState.ACTION_SEEK_TO

            val playbackState = PlaybackState.Builder()
                .setActions(actions)
                .setState(state, positionMicros / 1000L, if (isPlaying) 1.0f else 0.0f)
                .build()

            session.setPlaybackState(playbackState)
            session.isActive = true

            val metadata = session.controller.metadata
            val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: ""
            val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""
            val cover = metadata?.getBitmap(MediaMetadata.METADATA_KEY_ART)
            updateNotification(context, title, artist, cover, isPlaying)
        }

        private fun updateNotification(context: Context, title: String, artist: String, cover: Bitmap?, isPlaying: Boolean? = null) {
            val session = mediaSession ?: return
            val playing = isPlaying ?: (session.controller.playbackState?.state == PlaybackState.STATE_PLAYING)

            val builder = Notification.Builder(context, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_media_play)
                .setContentTitle(title.ifEmpty { "Unknown track" })
                .setContentText(artist.ifEmpty { "LibreConnect" })
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

            builder.addAction(Notification.Action.Builder(
                android.R.drawable.ic_media_previous, "Previous", buildPendingIntent(context, 88)
            ).build())

            if (playing) {
                builder.addAction(Notification.Action.Builder(
                    android.R.drawable.ic_media_pause, "Pause", buildPendingIntent(context, 85)
                ).build())
            } else {
                builder.addAction(Notification.Action.Builder(
                    android.R.drawable.ic_media_play, "Play", buildPendingIntent(context, 85)
                ).build())
            }

            builder.addAction(Notification.Action.Builder(
                android.R.drawable.ic_media_next, "Next", buildPendingIntent(context, 87)
            ).build())

            if (cover != null)
                builder.setLargeIcon(cover)

            val manager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
            manager.notify(NOTIFICATION_ID, builder.build())
        }

        private fun buildPendingIntent(context: Context, keyCode: Int): PendingIntent {
            val intent = Intent(context, RemoteInputNotificationReceiver::class.java).apply {
                action = "com.LibreConnect.MEDIA_ACTION"
                putExtra("KEYCODE", keyCode)
            }

            return PendingIntent.getBroadcast(context, keyCode, intent, PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)
        }

        private fun getMediaSession(context: Context): MediaSession {
            mediaSession?.let { return it }

            synchronized(this) {
                mediaSession?.let { return it }

                val session = MediaSession(context, "LibreConnectDesktopMedia").apply {
                    setCallback(object : MediaSession.Callback() {
                        override fun onPlay() { nativeOnMediaAction(85) }
                        override fun onPause() { nativeOnMediaAction(85) }
                        override fun onSkipToPrevious() { nativeOnMediaAction(88) }
                        override fun onSkipToNext() { nativeOnMediaAction(87) }
                        override fun onSeekTo(pos: Long) { nativeOnSeek(pos.toDouble() / 1000.0) }
                        override fun onMediaButtonEvent(mediaButtonIntent: Intent): Boolean {
                            return super.onMediaButtonEvent(mediaButtonIntent)
                        }
                    }, Handler(Looper.getMainLooper()))
                }

                mediaSession = session
                return session
            }
        }

        private fun decodeCover(coverBytes: ByteArray?): Bitmap? {
            if (coverBytes == null || coverBytes.isEmpty())
                return null

            return BitmapFactory.decodeByteArray(coverBytes, 0, coverBytes.size)
        }

        private fun createChannel(manager: NotificationManager) {
            if (manager.getNotificationChannel(CHANNEL_ID) == null) {
                val channel = NotificationChannel(CHANNEL_ID, "Remote Input", NotificationManager.IMPORTANCE_LOW)
                manager.createNotificationChannel(channel)
            }
        }

        @JvmStatic external fun nativeOnMediaAction(keyCode: Int)
        @JvmStatic external fun nativeOnSeek(positionSeconds: Double)
    }
}

class RemoteInputNotificationReceiver : android.content.BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == "com.LibreConnect.MEDIA_ACTION") {
            val keyCode = intent.getIntExtra("KEYCODE", 85)
            MediaNotificationBridge.nativeOnMediaAction(keyCode)
        }
    }
}
