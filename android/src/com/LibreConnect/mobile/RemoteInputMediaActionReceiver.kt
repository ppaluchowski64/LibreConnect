package com.LibreConnect.mobile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Build
import android.util.Log
import java.io.File

class RemoteInputMediaActionReceiver : BroadcastReceiver() {
    external fun onRemoteInputMediaActionCPP(action: String)

    companion object {
        private const val TAG = "RemoteInputMediaRcvr"

        @Volatile
        private var nativeLoaded = false

        private fun ensureNativeLoaded(context: Context): Boolean {
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

    override fun onReceive(context: Context?, intent: Intent?) {
        if (context == null) return
        if (!ensureNativeLoaded(context)) return

        val action = RemoteInputNotification.extractAction(intent) ?: return
        onRemoteInputMediaActionCPP(action)
    }
}
