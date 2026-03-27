package com.LibreConnect.mobile

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.Manifest
import android.util.Log
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean

class MainService : Service() {
    external fun nativeStartBackend()
    external fun nativeStopBackend()

    private val backendStarted = AtomicBoolean(false)
    private val cameraRequested = AtomicBoolean(false)
    private var notificationManager: NotificationManager? = null
    private var multicastLock: WifiManager.MulticastLock? = null

    override fun onCreate() {
        super.onCreate()
        startAsForeground()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (!ensureNativeLoaded(this)) {
            Log.e(TAG, "Native library not loaded, cannot start backend")
            stopSelf()
            return START_NOT_STICKY
        }

        if (intent?.action == ACTION_STOP_BACKEND) {
            stopBackendIfNeeded()
            stopSelf()
            return START_NOT_STICKY
        }

        if (intent?.action == ACTION_SET_CAMERA_REQUEST) {
            if (intent.hasExtra(EXTRA_REQUEST_CAMERA)) {
                cameraRequested.set(intent.getBooleanExtra(EXTRA_REQUEST_CAMERA, false))
                startAsForeground()
            }

            if (!backendStarted.get()) {
                stopSelf()
                return START_NOT_STICKY
            }

            return START_STICKY
        }

        if (intent?.hasExtra(EXTRA_REQUEST_CAMERA) == true) {
            cameraRequested.set(intent.getBooleanExtra(EXTRA_REQUEST_CAMERA, false))
            startAsForeground()
        }
        startBackendIfNeeded()

        return START_STICKY
    }

    override fun onDestroy() {
        stopBackendIfNeeded()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    private fun startAsForeground() {
        val manager = getSystemService(NotificationManager::class.java)
        notificationManager = manager
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "LibreConnect Service",
                NotificationManager.IMPORTANCE_LOW
            )
            manager.createNotificationChannel(channel)
        }

        val notification = buildNotification(
            title = "LibreConnect running",
            text = "Background service active"
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            val serviceTypes = resolveForegroundServiceTypes()

            startForeground(
                NOTIFICATION_ID,
                notification,
                serviceTypes
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    fun updateNotification(title: String, text: String) {
        val manager = notificationManager ?: getSystemService(NotificationManager::class.java).also {
            notificationManager = it
        }
        val notification = buildNotification(title = title, text = text)
        manager.notify(NOTIFICATION_ID, notification)
    }

    private fun buildNotification(title: String, text: String): Notification {
        val builder = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            Notification.Builder(this, CHANNEL_ID)
        } else {
            Notification.Builder(this)
        }

        return builder
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle(title)
            .setContentText(text)
            .setOngoing(true)
            .build()
    }

    private fun resolveForegroundServiceTypes(): Int {
        var types = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        if (shouldIncludeCameraType()) {
            types = types or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
        }
        return types
    }

    private fun startBackendIfNeeded() {
        if (!backendStarted.compareAndSet(false, true)) {
            return
        }

        acquireMulticastLock()
        nativeStartBackend()
    }

    private fun stopBackendIfNeeded() {
        if (backendStarted.compareAndSet(true, false)) {
            nativeStopBackend()
        }

        releaseMulticastLock()
    }

    private fun acquireMulticastLock() {
        if (multicastLock != null) {
            return
        }

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
        val lock = wifiManager.createMulticastLock("LibreConnectMulticastLock")
        lock.setReferenceCounted(false)
        lock.acquire()
        multicastLock = lock
    }

    private fun releaseMulticastLock() {
        multicastLock?.let {
            if (it.isHeld) {
                it.release()
            }
        }
        multicastLock = null
    }

    private fun shouldIncludeCameraType(): Boolean {
        if (!cameraRequested.get()) return false
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
        if (checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) return false
        return true
    }

    companion object {
        private const val TAG = "MainService"
        const val CHANNEL_ID = "libreconnect_main_service"
        const val NOTIFICATION_ID = 1001
        const val EXTRA_REQUEST_CAMERA = "com.LibreConnect.mobile.EXTRA_REQUEST_CAMERA"
        const val ACTION_START_BACKEND = "com.LibreConnect.mobile.action.START_BACKEND"
        const val ACTION_STOP_BACKEND = "com.LibreConnect.mobile.action.STOP_BACKEND"
        const val ACTION_SET_CAMERA_REQUEST = "com.LibreConnect.mobile.action.SET_CAMERA_REQUEST"

        @Volatile
        private var nativeLoaded = false

        fun ensureNativeLoaded(context: Context): Boolean {
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
                        Log.i(TAG, "Loaded native library via System.loadLibrary: $candidate")
                        return true
                    } catch (_: UnsatisfiedLinkError) {
                    }
                }

                val nativeDir = context.applicationInfo.nativeLibraryDir
                val fallback = File(nativeDir).listFiles()
                    ?.firstOrNull { it.isFile && it.name.startsWith("libLibreConnectNative") && it.name.endsWith(".so") }
                if (fallback != null) {
                    try {
                        System.load(fallback.absolutePath)
                        nativeLoaded = true
                        Log.i(TAG, "Loaded native library via absolute path: ${fallback.name}")
                        return true
                    } catch (e: UnsatisfiedLinkError) {
                        Log.e(TAG, "Failed to load native library from ${fallback.absolutePath}", e)
                    }
                }

                Log.e(TAG, "Unable to load LibreConnect native library. nativeLibraryDir=$nativeDir")
                return false
            }
        }
    }
}
