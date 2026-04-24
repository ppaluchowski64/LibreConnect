package com.LibreConnect.mobile

import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log

object ClipboardSyncDispatcher {
    private const val TAG = "ClipboardSyncDispatcher"

    @JvmStatic
    private external fun nativeRequestClipboardSyncWithText(text: String)

    @JvmStatic
    fun requestManualSync(context: Context, clipboardTextOverride: String? = null) {
        val appContext = context.applicationContext
        val clipboardText = clipboardTextOverride ?: ClipboardBridge.getClipboardText(appContext)

        val serviceIntent = Intent(appContext, MainService::class.java).apply {
            action = MainService.ACTION_START_BACKEND
        }

        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                appContext.startForegroundService(serviceIntent)
            } else {
                appContext.startService(serviceIntent)
            }
        } catch (t: Throwable) {
            Log.w(TAG, "Failed to start backend service before clipboard sync", t)
        }

        if (!MainService.ensureNativeLoaded(appContext)) {
            Log.e(TAG, "Failed to load native library for clipboard sync request")
            return
        }

        nativeRequestClipboardSyncWithText(clipboardText)
    }
}
