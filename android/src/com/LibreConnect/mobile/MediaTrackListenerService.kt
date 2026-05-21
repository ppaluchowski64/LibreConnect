package com.LibreConnect.mobile

import android.content.ComponentName
import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.service.notification.NotificationListenerService
import java.io.ByteArrayOutputStream

class MediaTrackListenerService : NotificationListenerService() {

    private var mediaSessionManager: MediaSessionManager? = null
    private var currentController: MediaController? = null

    companion object {
        private var instance: MediaTrackListenerService? = null

        @JvmStatic
        fun setPosition(positionMs: Long) {
            instance?.currentController?.transportControls?.seekTo(positionMs)
        }
    }

    private val sessionListener = MediaSessionManager.OnActiveSessionsChangedListener { controllers ->
        updateActiveController(controllers)
    }

    private val controllerCallback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            sendTrackInfo()
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            sendTrackInfo()
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        mediaSessionManager = getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
    }

    override fun onDestroy() {
        instance = null
        super.onDestroy()
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        val componentName = ComponentName(this, MediaTrackListenerService::class.java)

        try {
            mediaSessionManager?.addOnActiveSessionsChangedListener(sessionListener, componentName)
            updateActiveController(mediaSessionManager?.getActiveSessions(componentName))
        } catch (e: SecurityException) {}
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        mediaSessionManager?.removeOnActiveSessionsChangedListener(sessionListener)
        currentController?.unregisterCallback(controllerCallback)
        currentController = null
    }

    private fun updateActiveController(controllers: List<MediaController>?) {
        val remoteControllers = controllers
            ?.filterNot { it.packageName == packageName }
            .orEmpty()

        if (remoteControllers.isEmpty()) {
            currentController?.unregisterCallback(controllerCallback)
            currentController = null
            return
        }

        val activeController = remoteControllers.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: remoteControllers.firstOrNull()

        if (currentController != activeController) {
            currentController?.unregisterCallback(controllerCallback)
            currentController = activeController
            currentController?.registerCallback(controllerCallback)
            sendTrackInfo()
        }
    }

    private fun sendTrackInfo() {
        val controller = currentController ?: return
        val metadata = controller.metadata
        val state = controller.playbackState

        val title = metadata?.getString(MediaMetadata.METADATA_KEY_TITLE) ?: ""
        val artist = metadata?.getString(MediaMetadata.METADATA_KEY_ARTIST) ?: ""
        val album = metadata?.getString(MediaMetadata.METADATA_KEY_ALBUM) ?: ""
        val durationMs = metadata?.getLong(MediaMetadata.METADATA_KEY_DURATION) ?: 0L

        val positionMs = state?.position ?: 0L
        val isPlaying = state?.state == PlaybackState.STATE_PLAYING

        var coverData: ByteArray? = null
        val bitmap = metadata?.getBitmap(MediaMetadata.METADATA_KEY_ART)
            ?: metadata?.getBitmap(MediaMetadata.METADATA_KEY_ALBUM_ART)

        if (bitmap != null) {
            val stream = ByteArrayOutputStream()
            bitmap.compress(Bitmap.CompressFormat.JPEG, 80, stream)
            coverData = stream.toByteArray()
        }

        try {
            nativeOnTrackUpdate(title, artist, album, durationMs * 1000L, positionMs * 1000L, isPlaying, coverData)
        } catch (_: UnsatisfiedLinkError) {}
    }

    external fun nativeOnTrackUpdate(
        title: String,
        artist: String,
        album: String,
        durationMicros: Long,
        positionMicros: Long,
        isPlaying: Boolean,
        coverData: ByteArray?
    )
}
