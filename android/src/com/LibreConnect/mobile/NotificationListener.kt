package com.LibreConnect.mobile
import android.app.Notification
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.graphics.drawable.Icon
import android.media.MediaMetadata
import android.media.session.MediaController
import android.media.session.MediaSessionManager
import android.media.session.PlaybackState
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import java.io.ByteArrayOutputStream
import java.io.File
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.provider.Settings
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.qtproject.qt.android.bindings.QtActivity

class NotificationListener : NotificationListenerService() {
    private val tag = "NotificationListener"
    private val extraSubstituteAppNameKey = "android.substName"
    private val trackedNotificationKeys = mutableSetOf<String>()
    private var initialSyncCompleted = false
    private var mediaSessionManager: MediaSessionManager? = null
    private var currentMediaController: MediaController? = null

    external fun onNotificationReceivedCPP(key: String, appName: String?, title: String?, content: String?, timestamp: Long, dismissable: Boolean, largeIconBytes: ByteArray?, mainImage: ByteArray?)
    external fun onNotificationRemovedCPP(key: String)
    external fun nativeOnTrackUpdate(
        title: String,
        artist: String,
        album: String,
        durationMicros: Long,
        positionMicros: Long,
        isPlaying: Boolean,
        coverData: ByteArray?
    )

    companion object {
        @Volatile
        private var nativeLoaded = false

        @Volatile
        private var instance: NotificationListener? = null

        @JvmStatic
        fun setPosition(positionMs: Long) {
            instance?.currentMediaController?.transportControls?.seekTo(positionMs)
        }

        @JvmStatic
        fun requestSync(context: Context) {
            if (instance == null) {
                val pm = context.packageManager
                val component = ComponentName(context, NotificationListener::class.java)

                pm.setComponentEnabledSetting(
                    component,
                    PackageManager.COMPONENT_ENABLED_STATE_DISABLED,
                    PackageManager.DONT_KILL_APP
                )

                android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                    try {
                        pm.setComponentEnabledSetting(
                            component,
                            PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
                            PackageManager.DONT_KILL_APP
                        )

                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                            android.service.notification.NotificationListenerService.requestRebind(component)
                        }

                        Log.i("NotificationListener", "Successfully kicked the listener service back awake.")

                    } catch (e: Exception) {
                        Log.e("NotificationListener", "Failed to restart service", e)
                    }
                }, 500)
            } else {
                instance?.syncActiveNotificationsToCpp(force = true)
            }
        }

        @JvmStatic
        fun dismissNotification(context: Context, key: String) {
            val listener = instance
            if (listener != null) {
                listener.dismissNotificationInternal(key)
                return
            }

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                val component = ComponentName(context, NotificationListener::class.java)
                android.service.notification.NotificationListenerService.requestRebind(component)
            }
        }

        private fun ensureNativeLoaded(service: NotificationListenerService): Boolean {
            if (nativeLoaded) return true

            synchronized(this) {
                if (nativeLoaded) return true

                val candidates = mutableListOf("LibreConnectNative")
                Build.SUPPORTED_ABIS.forEach { abi ->
                    candidates.add("LibreConnectNative_$abi")
                }

                for (candidate in candidates) {
                    try {
                        System.loadLibrary(candidate)
                        nativeLoaded = true
                        Log.i("NotificationListener", "Loaded native library via System.loadLibrary: $candidate")
                        return true
                    } catch (_: UnsatisfiedLinkError) {
                        // Try next candidate.
                    }
                }

                val nativeDir = service.applicationInfo.nativeLibraryDir
                val fallback = File(nativeDir).listFiles()
                    ?.firstOrNull { it.isFile && it.name.startsWith("libLibreConnectNative") && it.name.endsWith(".so") }
                if (fallback != null) {
                    try {
                        System.load(fallback.absolutePath)
                        nativeLoaded = true
                        Log.i("NotificationListener", "Loaded native library via absolute path: ${fallback.name}")
                        return true
                    } catch (e: UnsatisfiedLinkError) {
                        Log.e("NotificationListener", "Failed to load native library from ${fallback.absolutePath}", e)
                    }
                }

                Log.e("NotificationListener", "Unable to load LibreConnect native library. nativeLibraryDir=$nativeDir")
                return false
            }
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        mediaSessionManager = getSystemService(Context.MEDIA_SESSION_SERVICE) as MediaSessionManager
        Log.i(tag, "Notification listener created.")
    }

    override fun onDestroy() {
        stopMediaSessionTracking()
        instance = null
        super.onDestroy()
        Log.i(tag, "Notification listener destroyed.")
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!ensureNativeLoaded(this)) {
            Log.e(tag, "Skipping notification posted callback because native library is not loaded.")
            return
        }

        if (!initialSyncCompleted) {
            syncActiveNotificationsToCpp(force = true)
            if (initialSyncCompleted) {
                return
            }
        }

        dispatchNotificationPosted(sbn)
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        Log.i(tag, "Notification listener connected.")
        startMediaSessionTracking()

        if (!ensureNativeLoaded(this)) {
            Log.e(tag, "Skipping listener sync because native library is not loaded.")
            return
        }

        syncActiveNotificationsToCpp(force = true)
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        Log.w(tag, "Notification listener disconnected.")
        trackedNotificationKeys.clear()
        initialSyncCompleted = false
        stopMediaSessionTracking()
    }

    private val mediaSessionListener = MediaSessionManager.OnActiveSessionsChangedListener { controllers ->
        updateActiveMediaController(controllers)
    }

    private val mediaControllerCallback = object : MediaController.Callback() {
        override fun onPlaybackStateChanged(state: PlaybackState?) {
            sendTrackInfo()
        }

        override fun onMetadataChanged(metadata: MediaMetadata?) {
            sendTrackInfo()
        }
    }

    private fun startMediaSessionTracking() {
        val componentName = ComponentName(this, NotificationListener::class.java)

        try {
            mediaSessionManager?.removeOnActiveSessionsChangedListener(mediaSessionListener)
            mediaSessionManager?.addOnActiveSessionsChangedListener(mediaSessionListener, componentName)
            updateActiveMediaController(mediaSessionManager?.getActiveSessions(componentName))
        } catch (e: SecurityException) {
            Log.w(tag, "Unable to track active media sessions.", e)
        }
    }

    private fun stopMediaSessionTracking() {
        mediaSessionManager?.removeOnActiveSessionsChangedListener(mediaSessionListener)
        currentMediaController?.unregisterCallback(mediaControllerCallback)
        currentMediaController = null
    }

    private fun updateActiveMediaController(controllers: List<MediaController>?) {
        val remoteControllers = controllers
            ?.filterNot { 
                it.packageName == packageName || 
                it.packageName == "com.LibreConnect.mobile" ||
                (MediaNotificationBridge.mediaSession != null && it.sessionToken == MediaNotificationBridge.mediaSession?.sessionToken)
            }
            .orEmpty()

        if (remoteControllers.isEmpty()) {
            currentMediaController?.unregisterCallback(mediaControllerCallback)
            currentMediaController = null
            return
        }

        val activeController = remoteControllers.firstOrNull {
            it.playbackState?.state == PlaybackState.STATE_PLAYING
        } ?: remoteControllers.firstOrNull()

        if (currentMediaController != activeController) {
            currentMediaController?.unregisterCallback(mediaControllerCallback)
            currentMediaController = activeController
            currentMediaController?.registerCallback(mediaControllerCallback)
            sendTrackInfo()
        }
    }

    private fun sendTrackInfo() {
        if (!ensureNativeLoaded(this)) {
            Log.e(tag, "Skipping media track callback because native library is not loaded.")
            return
        }

        val controller = currentMediaController ?: return
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
        } catch (e: UnsatisfiedLinkError) {
            Log.e(tag, "JNI method nativeOnTrackUpdate is missing or failed.", e)
        }
    }

    private fun syncActiveNotificationsToCpp(force: Boolean = false) {
        if (force) {
            trackedNotificationKeys.clear()
        }

        try {
            val currentNotifications = activeNotifications ?: emptyArray()
            val currentKeys = mutableSetOf<String>()

            currentNotifications.forEach { sbn ->
                if (shouldIgnoreNotification(sbn)) {
                    return@forEach
                }

                currentKeys.add(sbn.key)
                dispatchNotificationPosted(sbn)
            }

            trackedNotificationKeys.retainAll(currentKeys)
            initialSyncCompleted = true
            Log.i(tag, "Initial notification sync complete. count=${currentNotifications.size}")
        } catch (e: Exception) {
            Log.e(tag, "Failed to synchronize existing notifications.", e)
        }
    }

    private fun dispatchNotificationPosted(sbn: StatusBarNotification) {
        try {
            if (shouldIgnoreNotification(sbn)) {
                trackedNotificationKeys.remove(sbn.key)
                return
            }

            val key: String = sbn.key
            trackedNotificationKeys.add(key)

            val notification = sbn.notification
            val extras = notification.extras

            val appName = resolveAppName(sbn)
            val title: String? = firstMeaningfulText(
                extras.getCharSequence(Notification.EXTRA_TITLE)?.toString(),
                extras.getCharSequence(Notification.EXTRA_TITLE_BIG)?.toString(),
                extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()
            )
            val content: String? = firstMeaningfulText(
                extras.getCharSequence(Notification.EXTRA_TEXT)?.toString(),
                extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()
            )
            val timestamp: Long = sbn.postTime
            val largeIcon = notification.getLargeIcon()
            var mainImageBytes: ByteArray? = null

            val bigPicture = if (android.os.Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                extras.getParcelable(Notification.EXTRA_PICTURE_ICON, Icon::class.java)
            } else {
                extras.getParcelable(Notification.EXTRA_PICTURE) as? Bitmap
            }

            if (bigPicture != null) {
                val bitmap: Bitmap? = when (bigPicture) {
                    is Icon -> bitmapFromIcon(bigPicture, key, "big picture")
                    is Bitmap -> bigPicture
                    else -> null
                }

                if (bitmap != null) {
                    mainImageBytes = if (bitmap.width > 1024) {
                        val aspectRatio = bitmap.height.toDouble() / bitmap.width.toDouble()
                        val scaledBitmap = bitmap.scale(1024, (1024 * aspectRatio).toInt())
                        bitmapToByteArray(scaledBitmap)
                    } else {
                        bitmapToByteArray(bitmap)
                    }
                }
            }

            val iconBitmap = bitmapFromIcon(largeIcon, key, "large")
                ?: bitmapFromIcon(notification.smallIcon, key, "small")
            val iconBytes: ByteArray? = iconBitmap?.let { bitmapToByteArray(it) }

            try {
                onNotificationReceivedCPP(
                    key,
                    appName,
                    title,
                    content,
                    timestamp,
                    sbn.isClearable,
                    iconBytes,
                    mainImageBytes
                )
            } catch (e: UnsatisfiedLinkError) {
                Log.e(tag, "JNI method onNotificationReceivedCPP is missing or failed.", e)
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to dispatch posted notification.", e)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (!ensureNativeLoaded(this)) {
            Log.e(tag, "Skipping notification removed callback because native library is not loaded.")
            return
        }

        if (shouldIgnoreNotification(sbn)) {
            trackedNotificationKeys.remove(sbn.key)
            return
        }

        val key = sbn.key
        val wasTracked = trackedNotificationKeys.remove(key)
        if (!wasTracked && initialSyncCompleted) {
            return
        }

        notifyCppNotificationRemoved(key)
    }

    private fun dismissNotificationInternal(key: String) {
        val target = activeNotifications?.firstOrNull { it.key == key }
        if (target == null) {
            val wasTrackedMissing = trackedNotificationKeys.remove(key)
            if (wasTrackedMissing) {
                notifyCppNotificationRemoved(key)
            }
            return
        }

        if (!target.isClearable) {
            Log.i(tag, "Notification is not dismissable for key=$key")
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            try {
                cancelNotification(key)
            } catch (e: Exception) {
                Log.w(tag, "Failed to dismiss notification for key=$key", e)
                return
            }
        }

        if (activeNotifications?.any { it.key == key } == true) {
            Log.i(tag, "Notification still active after dismiss request for key=$key")
            return
        }
    }

    private fun notifyCppNotificationRemoved(key: String) {
        try {
            onNotificationRemovedCPP(key)
        } catch (e: UnsatisfiedLinkError) {
            Log.e(tag, "JNI method onNotificationRemovedCPP is missing or failed.", e)
        }
    }

    private fun resolveAppName(sbn: StatusBarNotification): String? {
        val packageName = sbn.packageName
        if (packageName == "android" || packageName == "com.android.systemui") {
            return "Android System"
        }

        val substituteName = firstMeaningfulText(
            sbn.notification.extras.getCharSequence(extraSubstituteAppNameKey)?.toString()
        )
        if (!substituteName.isNullOrEmpty()) {
            return substituteName
        }

        return try {
            val applicationInfo = packageManager.getApplicationInfo(packageName, 0)
            val label = packageManager.getApplicationLabel(applicationInfo)?.toString()?.trim().orEmpty()
            if (label.isNotEmpty() && label != packageName) {
                label
            } else {
                humanizePackageName(packageName)
            }
        } catch (e: Exception) {
            humanizePackageName(packageName)
        }
    }

    private fun shouldIgnoreNotification(sbn: StatusBarNotification): Boolean {
        if (sbn.packageName == packageName || sbn.packageName == "com.LibreConnect.mobile") {
            return true
        }

        val notification = sbn.notification
        val extras = notification.extras
        val title = firstMeaningfulText(
            extras.getCharSequence(Notification.EXTRA_TITLE)?.toString(),
            extras.getCharSequence(Notification.EXTRA_TITLE_BIG)?.toString()
        )
        val body = firstMeaningfulText(
            extras.getCharSequence(Notification.EXTRA_TEXT)?.toString(),
            extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString(),
            extras.getCharSequence(Notification.EXTRA_SUB_TEXT)?.toString()
        )
        val groupSummary = (notification.flags and Notification.FLAG_GROUP_SUMMARY) != 0
        val noVisibleContent = title.isNullOrEmpty() && body.isNullOrEmpty()
        val isSystemSource = sbn.packageName == "android" || sbn.packageName == "com.android.systemui"

        if (groupSummary && noVisibleContent) {
            return true
        }

        if (isSystemSource && noVisibleContent) {
            return true
        }

        return false
    }

    private fun firstMeaningfulText(vararg values: String?): String? {
        for (value in values) {
            val normalized = value?.trim()
            if (!normalized.isNullOrEmpty()) {
                return normalized
            }
        }
        return null
    }

    private fun humanizePackageName(packageName: String): String {
        if (packageName.isBlank()) {
            return "Unknown app"
        }

        val lastToken = packageName.substringAfterLast('.')
        val cleaned = lastToken.replace('_', ' ').replace('-', ' ').trim()
        if (cleaned.isEmpty()) {
            return packageName
        }

        return cleaned.split(' ')
            .filter { it.isNotBlank() }
            .joinToString(" ") { token ->
                token.replaceFirstChar { ch ->
                    if (ch.isLowerCase()) ch.titlecase() else ch.toString()
                }
            }
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

    private fun bitmapFromIcon(icon: Icon?, key: String, source: String): Bitmap? {
        if (icon == null) {
            return null
        }

        if (!canResolveIconPackage(icon, key, source)) {
            return null
        }

        return try {
            icon.loadDrawable(applicationContext)?.let { drawableToBitmap(it) }
        } catch (e: Exception) {
            Log.w(tag, "Failed to load $source icon for key=$key", e)
            null
        }
    }

    private fun canResolveIconPackage(icon: Icon, key: String, source: String): Boolean {
        if (icon.type != Icon.TYPE_RESOURCE) {
            return true
        }

        val pkg = icon.resPackage
        if (pkg.isNullOrEmpty() || pkg == packageName || pkg == "android") {
            return true
        }

        return try {
            packageManager.getApplicationInfo(pkg, 0)
            true
        } catch (e: PackageManager.NameNotFoundException) {
            Log.w(tag, "Skipping $source icon for key=$key. Package not visible: $pkg")
            false
        } catch (e: SecurityException) {
            Log.w(tag, "Skipping $source icon for key=$key due to package visibility restrictions: $pkg")
            false
        }
    }

    private fun bitmapToByteArray(bitmap: Bitmap): ByteArray {
        val stream = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        return stream.toByteArray()
    }
}
