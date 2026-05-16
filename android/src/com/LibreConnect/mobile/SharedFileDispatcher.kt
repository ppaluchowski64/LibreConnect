package com.LibreConnect.mobile

import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

object SharedFileDispatcher {
    private const val TAG = "SharedFileDispatcher"

    @JvmStatic
    private external fun nativePostSharedFile(path: String)

    @JvmStatic
    fun postSharedFile(context: Context, path: String): Boolean {
        val appContext = context.applicationContext
        ensureBackendStarted(appContext)

        if (!MainService.ensureNativeLoaded(appContext)) {
            Log.e(TAG, "Failed to load native library for shared file post")
            return false
        }

        return try {
            nativePostSharedFile(path)
            true
        } catch (t: Throwable) {
            Log.e(TAG, "Failed to post shared file: $path", t)
            false
        }
    }

    private fun ensureBackendStarted(context: Context) {
        val serviceIntent = Intent(context, MainService::class.java).apply {
            action = MainService.ACTION_START_BACKEND
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(serviceIntent)
            } else {
                context.startService(serviceIntent)
            }
        } catch (t: Throwable) {
            Log.w(TAG, "Failed to start backend service before shared file post", t)
        }
    }
}
