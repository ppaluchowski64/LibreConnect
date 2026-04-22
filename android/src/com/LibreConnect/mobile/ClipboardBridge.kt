package com.LibreConnect.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.util.Log

class ClipboardBridge {
    companion object {
        private const val TAG = "ClipboardBridge"

        private var clipboardListener: ClipboardManager.OnPrimaryClipChangedListener? = null

        @JvmStatic
        fun getClipboardText(context: Context): String {
            val clipboardManager = context.applicationContext.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                ?: return ""

            val clip = clipboardManager.primaryClip ?: return ""
            if (clip.itemCount <= 0) {
                return ""
            }

            return try {
                clip.getItemAt(0).coerceToText(context).toString()
            } catch (e: SecurityException) {
                Log.w(TAG, "Clipboard read blocked by Android", e)
                ""
            }
        }

        @JvmStatic
        fun setClipboardText(context: Context, text: String): Boolean {
            val clipboardManager = context.applicationContext.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                ?: return false

            return try {
                clipboardManager.setPrimaryClip(ClipData.newPlainText("LibreConnect", text))
                true
            } catch (e: SecurityException) {
                Log.w(TAG, "Clipboard write blocked by Android", e)
                false
            }
        }

        @JvmStatic
        fun hasClipboardText(context: Context): Boolean {
            val clipboardManager = context.applicationContext.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                ?: return false

            val description = clipboardManager.primaryClipDescription ?: return false
            return description.hasMimeType("text/plain")
        }

        @JvmStatic
        fun setClipboardListenerEnabled(context: Context, enabled: Boolean) {
            val clipboardManager = context.applicationContext.getSystemService(Context.CLIPBOARD_SERVICE) as? ClipboardManager
                ?: return

            if (enabled) {
                if (clipboardListener != null) {
                    return
                }

                clipboardListener = ClipboardManager.OnPrimaryClipChangedListener {
                    if (MainService.ensureNativeLoaded(context.applicationContext)) {
                        nativeOnClipboardChanged()
                    }
                }

                clipboardManager.addPrimaryClipChangedListener(clipboardListener)
                return
            }

            clipboardListener?.let { clipboardManager.removePrimaryClipChangedListener(it) }
            clipboardListener = null
        }

        @JvmStatic
        private external fun nativeOnClipboardChanged()
    }
}
