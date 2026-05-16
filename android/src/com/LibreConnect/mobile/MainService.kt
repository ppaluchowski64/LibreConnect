package com.LibreConnect.mobile

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.drawable.Icon
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.net.wifi.WifiManager
import android.os.Build
import android.os.IBinder
import android.os.PowerManager
import android.Manifest
import android.annotation.SuppressLint
import android.util.Log
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class MainService : Service() {
    external fun nativeConfigureStorage(storageRootPath: String)
    external fun nativeStartBackend()
    external fun nativeStopBackend()
    external fun nativeShareLogs()
    external fun nativeOnAudioCaptured(samples: ByteArray)
    external fun nativeOnCameraEncodedSample(
        encodedSample: ByteBuffer,
        size: Int,
        flags: Int,
        ptsUs: Long
    )

    private val backendStarted = AtomicBoolean(false)
    private val cameraRequested = AtomicBoolean(false)
    private var notificationManager: NotificationManager? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var cpuWakeLock: PowerManager.WakeLock? = null

    override fun onCreate() {
        super.onCreate()
        activeService = this
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
        if (activeService === this) {
            activeService = null
        }
        stopBackendIfNeeded()
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        if (backendStarted.get()) {
            val restartIntent = Intent(applicationContext, MainService::class.java).apply {
                action = ACTION_START_BACKEND
            }

            try {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    applicationContext.startForegroundService(restartIntent)
                } else {
                    applicationContext.startService(restartIntent)
                }
            } catch (t: Throwable) {
                Log.e(TAG, "Failed to restart service after task removal", t)
            }
        }

        super.onTaskRemoved(rootIntent)
    }

    override fun onBind(intent: Intent?): IBinder? {
        return null
    }

    private fun startAsForeground() {
        val manager = getSystemService(NotificationManager::class.java)
        notificationManager = manager
        val channel = NotificationChannel(
            CHANNEL_ID,
            "LibreConnect Service",
            NotificationManager.IMPORTANCE_LOW
        )
        manager.createNotificationChannel(channel)

        val notification = buildNotification(
            title = "LibreConnect running",
            text = "Background service active"
        )

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            var serviceTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            if (shouldIncludeCameraType()) {
                serviceTypes = serviceTypes or ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA
            }
            startForeground(
                NOTIFICATION_ID,
                notification,
                serviceTypes
            )
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            startForeground(
                NOTIFICATION_ID,
                notification,
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
    }

    @Suppress("unused")
    fun updateNotification(title: String, text: String) {
        val manager = notificationManager ?: getSystemService(NotificationManager::class.java).also {
            notificationManager = it
        }
        val notification = buildNotification(title = title, text = text)
        manager.notify(NOTIFICATION_ID, notification)
    }

    private fun buildNotification(title: String, text: String): Notification {
        val builder = Notification.Builder(this, CHANNEL_ID)
        val clipboardIntent = ClipboardActionActivity.createLaunchIntent(this)
        val clipboardPendingIntent = PendingIntent.getActivity(
            this,
            2,
            clipboardIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return builder
            .setSmallIcon(android.R.drawable.stat_notify_sync)
            .setContentTitle(title)
            .setContentText(text)
            .addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(this, android.R.drawable.stat_notify_sync),
                    "Send Clipboard",
                    clipboardPendingIntent
                ).build()
            )
            .setOngoing(true)
            .build()
    }

    private fun startBackendIfNeeded() {
        if (!backendStarted.compareAndSet(false, true)) {
            return
        }

        val storageDir = getExternalFilesDir(null) ?: filesDir
        nativeConfigureStorage(storageDir.absolutePath)
        acquireMulticastLock()
        acquireWifiLock()
        acquireCpuWakeLock()
        nativeStartBackend()
    }

    private fun stopBackendIfNeeded() {
        if (backendStarted.compareAndSet(true, false)) {
            nativeStopBackend()
        }

        releaseMulticastLock()
        releaseWifiLock()
        releaseCpuWakeLock()
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

    private fun acquireWifiLock() {
        if (wifiLock?.isHeld == true) {
            return
        }

        val wifiManager = applicationContext.getSystemService(Context.WIFI_SERVICE) as? WifiManager ?: return
        @Suppress("DEPRECATION")
        val lock = wifiManager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "LibreConnect:MainServiceWifi")
        lock.setReferenceCounted(false)
        lock.acquire()
        wifiLock = lock
    }

    private fun releaseWifiLock() {
        wifiLock?.let {
            if (it.isHeld) {
                it.release()
            }
        }
        wifiLock = null
    }

    private fun acquireCpuWakeLock() {
        if (cpuWakeLock?.isHeld == true) {
            return
        }

        val powerManager = applicationContext.getSystemService(Context.POWER_SERVICE) as? PowerManager ?: return
        val lock = powerManager.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "LibreConnect:MainServiceCpu")
        lock.setReferenceCounted(false)

        try {
            lock.acquire(CPU_WAKE_LOCK_TIMEOUT_MS)
            cpuWakeLock = lock
        } catch (e: SecurityException) {
            Log.e(TAG, "Failed to acquire CPU wake lock", e)
        }
    }

    private fun releaseCpuWakeLock() {
        cpuWakeLock?.let {
            if (it.isHeld) {
                it.release()
            }
        }
        cpuWakeLock = null
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
        @Suppress("unused")
        const val ACTION_START_BACKEND = "com.LibreConnect.mobile.action.START_BACKEND"
        const val ACTION_STOP_BACKEND = "com.LibreConnect.mobile.action.STOP_BACKEND"
        const val ACTION_SET_CAMERA_REQUEST = "com.LibreConnect.mobile.action.SET_CAMERA_REQUEST"
        private const val CPU_WAKE_LOCK_TIMEOUT_MS = 24L * 60L * 60L * 1000L

        @Volatile
        private var nativeLoaded = false
        @Volatile
        private var activeService: MainService? = null

        @Suppress("unused")
        @JvmStatic
        fun postTransferProgressNotification(
            key: String,
            title: String,
            content: String,
            bytesTransferred: Long,
            totalBytes: Long
        ): Boolean {
            val service = activeService ?: return false
            NotificationBridge.postTransferProgressNotification(
                service.applicationContext,
                key,
                title,
                content,
                bytesTransferred,
                totalBytes
            )
            return true
        }

        @Suppress("unused")
        @JvmStatic
        fun postTransferNotification(key: String, title: String, content: String, success: Boolean): Boolean {
            val service = activeService ?: return false
            NotificationBridge.postTransferNotification(service.applicationContext, key, title, content, success)
            return true
        }

        @Suppress("unused")
        @JvmStatic
        fun queryAvailableCameraConfigurations(context: Context): String {
            return CameraFrameReceiver.queryAvailableCameraConfigurations(context)
        }

        @Suppress("unused")
        @JvmStatic
        fun startCameraFrameReceiver(
            context: Context,
            requestedCameraId: String? = null,
            requestedWidth: Int = 1280,
            requestedHeight: Int = 720,
            requestedFps: Int = 30,
            requestedBitrate: Int = 2_000_000
        ): Boolean {
            return CameraFrameReceiver.start(
                context = context,
                requestedCameraId = requestedCameraId,
                requestedWidth = requestedWidth,
                requestedHeight = requestedHeight,
                requestedFps = requestedFps,
                requestedBitrate = requestedBitrate
            ) { encodedSample, size, flags, ptsUs ->
                activeService?.nativeOnCameraEncodedSample(
                    encodedSample = encodedSample,
                    size = size,
                    flags = flags,
                    ptsUs = ptsUs
                )
            }
        }

        @Suppress("unused")
        @JvmStatic
        fun stopCameraFrameReceiver() {
            CameraFrameReceiver.stop()
        }

        @JvmStatic
        fun shareLogs(context: Context) {
            FileSystemUtils.shareLogs(context)
        }

        @SuppressLint("UnsafeDynamicallyLoadedCode")
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
