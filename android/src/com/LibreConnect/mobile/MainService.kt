package com.LibreConnect.mobile

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.os.Build
import android.os.IBinder
import java.util.concurrent.atomic.AtomicBoolean
import android.content.pm.ServiceInfo

class MainService : Service() {
    //private external fun startBackend();

    private val backendStarted = AtomicBoolean(false)
    private var notificationManager: NotificationManager? = null

    override fun onCreate() {
        super.onCreate()
        startAsForeground()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (backendStarted.compareAndSet(false, true)) {
            //startBackend()
        }

        return START_STICKY
    }

    override fun onDestroy() {
        backendStarted.set(false)
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
            val serviceTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_CAMERA or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE

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

    private companion object {
        const val CHANNEL_ID = "libreconnect_main_service"
        const val NOTIFICATION_ID = 1001
    }
}
