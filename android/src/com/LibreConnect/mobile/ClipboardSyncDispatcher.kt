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
    private external fun nativeSendClipboardWithText(text: String)

    @JvmStatic
    fun requestManualSync(context: Context, clipboardTextOverride: String? = null) {
        val appContext = context.applicationContext
        val clipboardText = clipboardTextOverride ?: ClipboardBridge.getClipboardText(appContext)

        ensureBackendStarted(appContext)

        if (!MainService.ensureNativeLoaded(appContext)) {
            Log.e(TAG, "Failed to load native library for clipboard sync request")
            return
        }

        nativeRequestClipboardSyncWithText(clipboardText)
    }

    @JvmStatic
    fun requestSendOnlySync(context: Context, clipboardText: String) {
        val appContext = context.applicationContext

        ensureBackendStarted(appContext)

        if (!MainService.ensureNativeLoaded(appContext)) {
            Log.e(TAG, "Failed to load native library for clipboard send request")
            return
        }

        nativeSendClipboardWithText(clipboardText)
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
            Log.w(TAG, "Failed to start backend service before clipboard operation", t)
        }
    }
}
