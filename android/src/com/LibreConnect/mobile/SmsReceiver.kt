package com.LibreConnect.mobile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.provider.Telephony
import android.util.Log

class SmsReceiver : BroadcastReceiver() {
    private val tag = "SmsReceiver"
    external fun onSmsReceivedCPP(sender: String?, body: String, timestamp: Long)

    override fun onReceive(context: Context, intent: Intent) {
        Log.d(tag, "onReceive: ${intent.action}")
        if (intent.action == Telephony.Sms.Intents.SMS_RECEIVED_ACTION) {
            if (!MainService.ensureNativeLoaded(context)) {
                Log.e(tag, "Native library not loaded, cannot process SMS")
                return
            }

            val messages = Telephony.Sms.Intents.getMessagesFromIntent(intent)
            Log.d(tag, "Received ${messages?.size ?: 0} messages")

            for (sms in messages) {
                try {
                    onSmsReceivedCPP(sms.originatingAddress, sms.messageBody, sms.timestampMillis)
                } catch (e: Exception) {
                    Log.e(tag, "Error calling onSmsReceivedCPP", e)
                }
            }
        }
    }
}
