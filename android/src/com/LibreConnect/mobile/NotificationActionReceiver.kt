package com.LibreConnect.mobile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

class NotificationActionReceiver : BroadcastReceiver() {
    companion object {
        private const val TAG = "NotificationActionRcvr"
    }

    override fun onReceive(context: Context?, intent: Intent?) {
        if (context == null || intent == null) return

        val key = intent.getStringExtra("notification_key") ?: return
        val option = intent.getStringExtra("notification_option") ?: return

        Log.d(TAG, "Forwarding notification action to backend: key=$key, option=$option")

        val serviceIntent = Intent(context, MainService::class.java).apply {
            action = MainService.ACTION_NOTIFICATION_ACTION
            putExtra(MainService.EXTRA_NOTIFICATION_KEY, key)
            putExtra(MainService.EXTRA_NOTIFICATION_OPTION, option)
        }

        try {
            context.startService(serviceIntent)
        } catch (e: Exception) {
            Log.e(TAG, "Failed to forward notification action to backend service", e)
        }
    }
}
