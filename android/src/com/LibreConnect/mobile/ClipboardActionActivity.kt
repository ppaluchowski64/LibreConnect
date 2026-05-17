package com.LibreConnect.mobile

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.util.Log

class ClipboardActionActivity : Activity() {
    private var syncDispatched = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        overridePendingTransition(0, 0)
        Log.i(TAG, "Clipboard action activity created; waiting for focus")
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            dispatchClipboardSync()
        }
    }

    private fun dispatchClipboardSync() {
        if (syncDispatched || isFinishing) {
            return
        }

        syncDispatched = true

        val clipboardText = ClipboardBridge.getClipboardText(this)
        Log.i(TAG, "Dispatching clipboard send (${clipboardText.length} chars)")
        ClipboardSyncDispatcher.requestSendOnlySync(this, clipboardText)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            finishAndRemoveTask()
        } else {
            finish()
        }
        overridePendingTransition(0, 0)
    }

    companion object {
        private const val TAG = "ClipboardActionActivity"

        @JvmStatic
        fun createLaunchIntent(context: Context): Intent {
            return Intent(context, ClipboardActionActivity::class.java).apply {
                addFlags(
                    Intent.FLAG_ACTIVITY_NEW_TASK or
                        Intent.FLAG_ACTIVITY_MULTIPLE_TASK or
                        Intent.FLAG_ACTIVITY_NO_ANIMATION or
                        Intent.FLAG_ACTIVITY_EXCLUDE_FROM_RECENTS
                )
            }
        }
    }
}
