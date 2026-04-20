package com.LibreConnect.mobile

import android.app.Activity
import android.os.Bundle
import android.util.Log

class ClipboardActionActivity : Activity() {
    private external fun nativeOnClipboardTileClicked()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        Log.i(TAG, "Transparent Clipboard Activity created to acquire window focus")

        if (MainService.ensureNativeLoaded(this)) {
            nativeOnClipboardTileClicked()
        } else {
            Log.e(TAG, "Failed to load native library for clipboard action")
        }

        finish()
    }

    companion object {
        private const val TAG = "ClipboardActionActivity"
    }
}
