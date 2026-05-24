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
import android.content.BroadcastReceiver
import android.content.IntentFilter
import org.qtproject.qt.android.bindings.QtActivity
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.atomic.AtomicBoolean

class MainService : Service() {
    external fun nativeConfigureStorage(storageRootPath: String, logRootPath: String)
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
    external fun nativeDisableCameraModule()
    external fun nativeDisableMicrophoneModule()
    external fun nativeRespondConnectionPending(accepted: Boolean, challenge: String)
    external fun nativeRespondConnectionApproval(approved: Boolean)
    external fun nativeSetClipboardSyncEnabled(enabled: Boolean)
    external fun nativeRequestClipboardSync()
    external fun nativeSetNotificationSyncEnabled(enabled: Boolean)
    external fun nativeSendMediaSignal(signal: Int)
    external fun nativeMediaSeek(position: Double)
    external fun nativeMediaSetVolume(volume: Int)
    external fun nativeSendKeyInput(key: Int, text: String, modifiers: Int)
    external fun nativeRefreshDownloadPath()
    external fun nativeSetDownloadPath(path: String)
    external fun nativeDisconnect()
    external fun nativeSetMirroringEnabled(enabled: Boolean)
    external fun nativeNotificationAction(key: String, option: String)

    private val backendStarted = AtomicBoolean(false)
    private val cameraRequested = AtomicBoolean(false)
    private val microphoneRequested = AtomicBoolean(false)
    private var notificationManager: NotificationManager? = null
    private var multicastLock: WifiManager.MulticastLock? = null
    private var wifiLock: WifiManager.WifiLock? = null
    private var cpuWakeLock: PowerManager.WakeLock? = null

    private val moduleActionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            when (intent?.action) {
                ACTION_DISABLE_CAMERA -> {
                    Log.d(TAG, "Disable camera action received from notification")
                    if (ensureNativeLoaded(this@MainService)) {
                        nativeDisableCameraModule()
                    }
                }
                ACTION_DISABLE_MICROPHONE -> {
                    Log.d(TAG, "Disable microphone action received from notification")
                    if (ensureNativeLoaded(this@MainService)) {
                        nativeDisableMicrophoneModule()
                    }
                }
            }
        }
    }

    override fun onCreate() {
        Log.d(TAG, "onCreate: start")
        super.onCreate()
        activeService = this
        val filter = IntentFilter().apply {
            addAction(ACTION_DISABLE_CAMERA)
            addAction(ACTION_DISABLE_MICROPHONE)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(moduleActionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(moduleActionReceiver, filter)
        }
        Log.d(TAG, "onCreate: calling startAsForeground")
        startAsForeground()
        Log.d(TAG, "onCreate: done")
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val action = intent?.action
        val extras = intent?.extras
        Log.d(TAG, "onStartCommand: action=$action, startId=$startId")

        // Parse camera requests synchronously on the main thread before starting foreground
        if (action == ACTION_SET_CAMERA_REQUEST) {
            if (extras?.containsKey(EXTRA_REQUEST_CAMERA) == true) {
                val req = extras.getBoolean(EXTRA_REQUEST_CAMERA, false)
                Log.d(TAG, "onStartCommand (sync): SET_CAMERA_REQUEST: $req")
                cameraRequested.set(req)
            }
        } else if (extras?.containsKey(EXTRA_REQUEST_CAMERA) == true) {
            val req = extras.getBoolean(EXTRA_REQUEST_CAMERA, false)
            Log.d(TAG, "onStartCommand (sync): extras camera request: $req")
            cameraRequested.set(req)
        }

        if (action == ACTION_RESPOND_CONNECTION_PENDING) {
            if (ensureNativeLoaded(this)) {
                nativeRespondConnectionPending(
                    extras?.getBoolean(EXTRA_ACCEPTED, false) ?: false,
                    extras?.getString(EXTRA_CHALLENGE).orEmpty()
                )
            }
            return START_STICKY
        }

        if (action == ACTION_RESPOND_CONNECTION_APPROVAL) {
            if (ensureNativeLoaded(this)) {
                nativeRespondConnectionApproval(extras?.getBoolean(EXTRA_APPROVED, false) ?: false)
            }
            return START_STICKY
        }

        if (action == ACTION_TOGGLE_CLIPBOARD_SYNC) {
            if (ensureNativeLoaded(this)) {
                val enabled = extras?.getBoolean(EXTRA_ENABLED, false) ?: false
                nativeSetClipboardSyncEnabled(enabled)
            }
            return START_STICKY
        }

        if (action == ACTION_SYNC_CLIPBOARD) {
            if (ensureNativeLoaded(this)) {
                nativeRequestClipboardSync()
            }
            return START_STICKY
        }

        if (action == ACTION_SEND_LOCAL_CLIPBOARD) {
            if (ensureNativeLoaded(this)) {
                val text = extras?.getString(EXTRA_CLIPBOARD_TEXT).orEmpty()
                Log.d(TAG, "onStartCommand: ACTION_SEND_LOCAL_CLIPBOARD text length=${text.length}")
                ClipboardSyncDispatcher.nativeSendClipboardWithText(text)
            }
            return START_STICKY
        }

        if (action == ACTION_TOGGLE_NOTIFICATION_SYNC) {
            if (ensureNativeLoaded(this)) {
                val enabled = extras?.getBoolean(EXTRA_ENABLED, false) ?: false
                nativeSetNotificationSyncEnabled(enabled)
            }
            return START_STICKY
        }

        if (action == ACTION_SEND_MEDIA_SIGNAL) {
            if (ensureNativeLoaded(this)) {
                val signal = extras?.getInt(EXTRA_SIGNAL, 0) ?: 0
                nativeSendMediaSignal(signal)
            }
            return START_STICKY
        }

        if (action == ACTION_MEDIA_SEEK) {
            if (ensureNativeLoaded(this)) {
                val position = extras?.getDouble(EXTRA_POSITION, 0.0) ?: 0.0
                nativeMediaSeek(position)
            }
            return START_STICKY
        }

        if (action == ACTION_MEDIA_SET_VOLUME) {
            if (ensureNativeLoaded(this)) {
                val volume = extras?.getInt(EXTRA_VOLUME, 0) ?: 0
                nativeMediaSetVolume(volume)
            }
            return START_STICKY
        }

        if (action == ACTION_SEND_KEY_INPUT) {
            if (ensureNativeLoaded(this)) {
                val key = extras?.getInt(EXTRA_KEY, 0) ?: 0
                val text = extras?.getString(EXTRA_TEXT).orEmpty()
                val modifiers = extras?.getInt(EXTRA_MODIFIERS, 0) ?: 0
                nativeSendKeyInput(key, text, modifiers)
            }
            return START_STICKY
        }

        if (action == ACTION_REFRESH_DOWNLOAD_PATH) {
            if (ensureNativeLoaded(this)) {
                nativeRefreshDownloadPath()
            }
            return START_STICKY
        }

        if (action == ACTION_SET_DOWNLOAD_PATH) {
            if (ensureNativeLoaded(this)) {
                val path = extras?.getString(EXTRA_PATH).orEmpty()
                nativeSetDownloadPath(path)
            }
            return START_STICKY
        }

        if (action == ACTION_DISCONNECT) {
            if (ensureNativeLoaded(this)) {
                nativeDisconnect()
            }
            return START_STICKY
        }

        if (action == ACTION_SET_MIRRORING_ENABLED) {
            if (ensureNativeLoaded(this)) {
                val enabled = extras?.getBoolean(EXTRA_ENABLED, false) ?: false
                nativeSetMirroringEnabled(enabled)
            }
            return START_STICKY
        }

        if (action == ACTION_NOTIFICATION_ACTION) {
            if (ensureNativeLoaded(this)) {
                val key = extras?.getString(EXTRA_NOTIFICATION_KEY) ?: return START_STICKY
                val option = extras?.getString(EXTRA_NOTIFICATION_OPTION) ?: return START_STICKY
                Log.d(TAG, "onStartCommand (sync): NOTIFICATION_ACTION: key=$key, option=$option")
                nativeNotificationAction(key, option)
            }
            return START_STICKY
        }

        if (action == ACTION_STOP_BACKEND) {
            Thread {
                Log.d(TAG, "onStartCommand: async stop start")
                stopBackendIfNeeded()
                Log.d(TAG, "onStartCommand: async stop calling stopSelf")
                stopSelf()
                Log.d(TAG, "onStartCommand: async stop done, killing process")
                android.os.Process.killProcess(android.os.Process.myPid())
            }.start()
            Log.d(TAG, "onStartCommand: returning START_NOT_STICKY (stop action)")
            return START_NOT_STICKY
        }

        Log.d(TAG, "onStartCommand: calling startAsForeground")
        startAsForeground()

        if (action == ACTION_SET_CAMERA_REQUEST) {
            if (!backendStarted.get()) {
                Log.d(TAG, "onStartCommand: SET_CAMERA_REQUEST and backend not started, calling stopSelf")
                stopSelf()
            }
            return START_STICKY
        }

        Thread {
            Log.d(TAG, "onStartCommand: async startup start")
            if (!ensureNativeLoaded(this)) {
                Log.e(TAG, "Native library not loaded, cannot start backend")
                stopSelf()
                return@Thread
            }

            if (backendStarted.get()) {
                Log.d(TAG, "onStartCommand: backend already started, skipping")
                return@Thread
            }

            Log.d(TAG, "onStartCommand: calling startBackendIfNeeded")
            startBackendIfNeeded()
            Log.d(TAG, "onStartCommand: async startup done")
        }.start()

        Log.d(TAG, "onStartCommand: returning START_STICKY")
        return START_STICKY
    }

    override fun onDestroy() {
        Log.d(TAG, "onDestroy called")
        if (activeService === this) {
            activeService = null
        }
        try {
            unregisterReceiver(moduleActionReceiver)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to unregister moduleActionReceiver", e)
        }
        notificationManager?.cancel(NOTIFICATION_ID_CAMERA)
        notificationManager?.cancel(NOTIFICATION_ID_MICROPHONE)
        super.onDestroy()
    }

    override fun onTaskRemoved(rootIntent: Intent?) {
        Log.d(TAG, "onTaskRemoved called")
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
            if (shouldIncludeMicrophoneType()) {
                serviceTypes = serviceTypes or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            }
            startForeground(
                NOTIFICATION_ID,
                notification,
                serviceTypes
            )
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            var serviceTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
            if (shouldIncludeMicrophoneType()) {
                serviceTypes = serviceTypes or ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE
            }
            startForeground(
                NOTIFICATION_ID,
                notification,
                serviceTypes
            )
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }
        updateModuleNotifications()
    }

    private fun updateModuleNotifications() {
        val manager = notificationManager ?: getSystemService(NotificationManager::class.java).also {
            notificationManager = it
        }

        val modulesChannel = NotificationChannel(
            MODULES_CHANNEL_ID,
            "Active Modules",
            NotificationManager.IMPORTANCE_DEFAULT
        )
        manager.createNotificationChannel(modulesChannel)

        // Camera notification
        if (cameraRequested.get()) {
            val disableIntent = Intent(ACTION_DISABLE_CAMERA).setPackage(packageName)
            val pendingIntent = PendingIntent.getBroadcast(
                this,
                101,
                disableIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            val builder = Notification.Builder(this, MODULES_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_menu_camera)
                .setContentTitle("Virtual Camera Active")
                .setContentText("Your camera is being streamed to your PC")
                .setOngoing(true)
                .addAction(
                    Notification.Action.Builder(
                        Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel),
                        "Disable",
                        pendingIntent
                    ).build()
                )
            manager.notify(NOTIFICATION_ID_CAMERA, builder.build())
        } else {
            manager.cancel(NOTIFICATION_ID_CAMERA)
        }

        // Microphone notification
        if (microphoneRequested.get()) {
            val disableIntent = Intent(ACTION_DISABLE_MICROPHONE).setPackage(packageName)
            val pendingIntent = PendingIntent.getBroadcast(
                this,
                102,
                disableIntent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
            val builder = Notification.Builder(this, MODULES_CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_btn_speak_now)
                .setContentTitle("Virtual Microphone Active")
                .setContentText("Your microphone is being streamed to your PC")
                .setOngoing(true)
                .addAction(
                    Notification.Action.Builder(
                        Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel),
                        "Disable",
                        pendingIntent
                    ).build()
                )
            manager.notify(NOTIFICATION_ID_MICROPHONE, builder.build())
        } else {
            manager.cancel(NOTIFICATION_ID_MICROPHONE)
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

        val stopIntent = Intent(this, MainService::class.java).apply {
            action = ACTION_STOP_BACKEND
        }
        val stopPendingIntent = PendingIntent.getService(
            this,
            3,
            stopIntent,
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
            .addAction(
                Notification.Action.Builder(
                    Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel),
                    "Quit",
                    stopPendingIntent
                ).build()
            )
            .setOngoing(true)
            .build()
    }

    private fun startBackendIfNeeded() {
        if (!backendStarted.compareAndSet(false, true)) {
            return
        }

        val storageDir = filesDir
        val logDir = getExternalFilesDir(null) ?: filesDir
        nativeConfigureStorage(storageDir.absolutePath, logDir.absolutePath)
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

    private fun shouldIncludeMicrophoneType(): Boolean {
        if (!microphoneRequested.get()) return false
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return false
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) return false
        return true
    }

    companion object {
        private const val TAG = "MainService"
        const val CHANNEL_ID = "libreconnect_main_service"
        const val MODULES_CHANNEL_ID = "libreconnect_active_modules"
        const val NOTIFICATION_ID = 1001
        const val NOTIFICATION_ID_CAMERA = 1002
        const val NOTIFICATION_ID_MICROPHONE = 1003
        const val EXTRA_REQUEST_CAMERA = "com.LibreConnect.mobile.EXTRA_REQUEST_CAMERA"
        @Suppress("unused")
        const val ACTION_START_BACKEND = "com.LibreConnect.mobile.action.START_BACKEND"
        const val ACTION_STOP_BACKEND = "com.LibreConnect.mobile.action.STOP_BACKEND"
        const val ACTION_SET_CAMERA_REQUEST = "com.LibreConnect.mobile.action.SET_CAMERA_REQUEST"
        const val ACTION_DISABLE_CAMERA = "com.LibreConnect.mobile.action.DISABLE_CAMERA"
        const val ACTION_DISABLE_MICROPHONE = "com.LibreConnect.mobile.action.DISABLE_MICROPHONE"
        const val ACTION_RESPOND_CONNECTION_PENDING = "com.LibreConnect.mobile.action.RESPOND_CONNECTION_PENDING"
        const val ACTION_RESPOND_CONNECTION_APPROVAL = "com.LibreConnect.mobile.action.RESPOND_CONNECTION_APPROVAL"
        const val EXTRA_ACCEPTED = "com.LibreConnect.mobile.EXTRA_ACCEPTED"
        const val EXTRA_APPROVED = "com.LibreConnect.mobile.EXTRA_APPROVED"
        const val EXTRA_CHALLENGE = "com.LibreConnect.mobile.EXTRA_CHALLENGE"
        const val EXTRA_BACKEND_EVENT = "com.LibreConnect.mobile.EXTRA_BACKEND_EVENT"
        const val EXTRA_DEVICE_ID = "com.LibreConnect.mobile.EXTRA_DEVICE_ID"
        const val EXTRA_DEVICE_NAME = "com.LibreConnect.mobile.EXTRA_DEVICE_NAME"
        const val EXTRA_CONNECTION_MODE = "com.LibreConnect.mobile.EXTRA_CONNECTION_MODE"
        const val EXTRA_PAIRING_CODE = "com.LibreConnect.mobile.EXTRA_PAIRING_CODE"
        const val BACKEND_EVENT_CONNECTION_PENDING = "connection_pending"
        const val BACKEND_EVENT_CONNECTION_APPROVAL = "connection_approval"

        const val ACTION_TOGGLE_CLIPBOARD_SYNC = "com.LibreConnect.mobile.action.TOGGLE_CLIPBOARD_SYNC"
        const val ACTION_SYNC_CLIPBOARD = "com.LibreConnect.mobile.action.SYNC_CLIPBOARD"
        const val ACTION_SEND_LOCAL_CLIPBOARD = "com.LibreConnect.mobile.action.SEND_LOCAL_CLIPBOARD"
        const val ACTION_TOGGLE_NOTIFICATION_SYNC = "com.LibreConnect.mobile.action.TOGGLE_NOTIFICATION_SYNC"
        const val ACTION_SEND_MEDIA_SIGNAL = "com.LibreConnect.mobile.action.SEND_MEDIA_SIGNAL"
        const val ACTION_SEND_KEY_INPUT = "com.LibreConnect.mobile.action.SEND_KEY_INPUT"
        const val ACTION_MEDIA_SEEK = "com.LibreConnect.mobile.action.MEDIA_SEEK"
        const val ACTION_MEDIA_SET_VOLUME = "com.LibreConnect.mobile.action.MEDIA_SET_VOLUME"
        const val ACTION_REFRESH_DOWNLOAD_PATH = "com.LibreConnect.mobile.action.REFRESH_DOWNLOAD_PATH"
        const val ACTION_SET_DOWNLOAD_PATH = "com.LibreConnect.mobile.action.SET_DOWNLOAD_PATH"
        const val ACTION_DISCONNECT = "com.LibreConnect.mobile.action.DISCONNECT"
        const val ACTION_SET_MIRRORING_ENABLED = "com.LibreConnect.mobile.action.SET_MIRRORING_ENABLED"
        const val ACTION_NOTIFICATION_ACTION = "com.LibreConnect.mobile.action.NOTIFICATION_ACTION"

        const val EXTRA_ENABLED = "com.LibreConnect.mobile.EXTRA_ENABLED"
        const val EXTRA_SIGNAL = "com.LibreConnect.mobile.EXTRA_SIGNAL"
        const val EXTRA_KEY = "com.LibreConnect.mobile.EXTRA_KEY"
        const val EXTRA_TEXT = "com.LibreConnect.mobile.EXTRA_TEXT"
        const val EXTRA_CLIPBOARD_TEXT = "com.LibreConnect.mobile.EXTRA_CLIPBOARD_TEXT"
        const val EXTRA_MODIFIERS = "com.LibreConnect.mobile.EXTRA_MODIFIERS"
        const val EXTRA_POSITION = "com.LibreConnect.mobile.EXTRA_POSITION"
        const val EXTRA_VOLUME = "com.LibreConnect.mobile.EXTRA_VOLUME"
        const val EXTRA_PATH = "com.LibreConnect.mobile.EXTRA_PATH"
        const val EXTRA_NOTIFICATION_KEY = "com.LibreConnect.mobile.EXTRA_NOTIFICATION_KEY"
        const val EXTRA_NOTIFICATION_OPTION = "com.LibreConnect.mobile.EXTRA_NOTIFICATION_OPTION"

        private const val CPU_WAKE_LOCK_TIMEOUT_MS = 24L * 60L * 60L * 1000L

        @Volatile
        private var nativeLoaded = false
        @Volatile
        private var activeService: MainService? = null

        @JvmStatic
        fun setMicrophoneRequested(enabled: Boolean) {
            val service = activeService
            if (service != null) {
                service.microphoneRequested.set(enabled)
                android.os.Handler(android.os.Looper.getMainLooper()).post {
                    service.startAsForeground()
                }
            }
        }

        @JvmStatic
        fun getActiveContext(): Context? {
            return activeService?.applicationContext
        }

        @JvmStatic
        fun publishBackendConnectionPending(
            context: Context,
            deviceId: String,
            deviceName: String,
            connectionMode: Int,
            pairingCode: String
        ) {
            val intent = Intent(context, QtActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                putExtra(EXTRA_BACKEND_EVENT, BACKEND_EVENT_CONNECTION_PENDING)
                putExtra(EXTRA_DEVICE_ID, deviceId)
                putExtra(EXTRA_DEVICE_NAME, deviceName)
                putExtra(EXTRA_CONNECTION_MODE, connectionMode)
                putExtra(EXTRA_PAIRING_CODE, pairingCode)
            }
            context.startActivity(intent)
        }

        @JvmStatic
        fun publishBackendConnectionApproval(context: Context, deviceId: String, deviceName: String) {
            val intent = Intent(context, QtActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP)
                putExtra(EXTRA_BACKEND_EVENT, BACKEND_EVENT_CONNECTION_APPROVAL)
                putExtra(EXTRA_DEVICE_ID, deviceId)
                putExtra(EXTRA_DEVICE_NAME, deviceName)
            }
            context.startActivity(intent)
        }

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
        fun onAudioCaptured(samples: ByteArray) {
            activeService?.nativeOnAudioCaptured(samples)
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

                val abis = Build.SUPPORTED_ABIS

                try {
                    System.loadLibrary("c++_shared")
                } catch (e: UnsatisfiedLinkError) {
                    Log.w(TAG, "Failed to load c++_shared, might be built-in or missing")
                }

                var qtCoreLoaded = false
                for (abi in abis) {
                    try {
                        System.loadLibrary("Qt6Core_$abi")
                        qtCoreLoaded = true
                        Log.i(TAG, "Loaded Qt6Core_$abi")
                        break
                    } catch (_: UnsatisfiedLinkError) {
                    }
                }
                if (!qtCoreLoaded) {
                    try {
                        System.loadLibrary("Qt6Core")
                        Log.i(TAG, "Loaded Qt6Core")
                    } catch (e: UnsatisfiedLinkError) {
                        Log.w(TAG, "Failed to load Qt6Core explicitly, relying on auto-loading")
                    }
                }

                val candidates = mutableListOf("LibreConnectNative")
                abis.forEach { abi ->
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
