package com.LibreConnect.mobile
import android.app.Notification
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import android.os.Build
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import java.io.ByteArrayOutputStream
import java.io.File
import androidx.core.graphics.createBitmap
import androidx.core.graphics.scale
import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.provider.Settings
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import org.qtproject.qt.android.bindings.QtActivity

class NotificationListener : NotificationListenerService() {
    private val tag = "NotificationListener"
    private val trackedNotificationKeys = mutableSetOf<String>()
    private var initialSyncCompleted = false
    external fun onNotificationReceivedCPP(key: String, title: String?, content: String?, timestamp: Long, largeIconBytes: ByteArray?, mainImage: ByteArray?)
    external fun onNotificationRemovedCPP(key: String)

    companion object {
        @Volatile
        private var nativeLoaded = false

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
    }

    private fun syncActiveNotificationsToCpp(force: Boolean = false) {
        if (force) {
            trackedNotificationKeys.clear()
        }

        try {
            val currentNotifications = activeNotifications ?: emptyArray()
            val currentKeys = mutableSetOf<String>()

            currentNotifications.forEach { sbn ->
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
        val key: String = sbn.key
        trackedNotificationKeys.add(key)

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
            val bitmap: Bitmap? = if (bigPicture is android.graphics.drawable.Icon) {
                val drawable = bigPicture.loadDrawable(applicationContext)
                drawable?.let { drawableToBitmap(it) }
            } else {
                bigPicture as Bitmap
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

        try {
            onNotificationReceivedCPP(
                key,
                title,
                content,
                timestamp,
                iconBytes,
                mainImageBytes
            )
        } catch (e: UnsatisfiedLinkError) {
            Log.e(tag, "JNI method onNotificationReceivedCPP is missing or failed.", e)
        }
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        if (!ensureNativeLoaded(this)) {
            Log.e(tag, "Skipping notification removed callback because native library is not loaded.")
            return
        }

        val key = sbn.key
        trackedNotificationKeys.remove(key)

        try {
            onNotificationRemovedCPP(
                key
            )
        } catch (e: UnsatisfiedLinkError) {
            Log.e(tag, "JNI method onNotificationRemovedCPP is missing or failed.", e)
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

    private fun bitmapToByteArray(bitmap: Bitmap): ByteArray {
        val stream = ByteArrayOutputStream()
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream)
        return stream.toByteArray()
    }
}
