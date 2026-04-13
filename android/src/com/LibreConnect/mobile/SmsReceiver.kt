package com.LibreConnect.mobile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.provider.Telephony

class SmsReceiver : BroadcastReceiver() {
    external fun onSmsReceivedCPP(sender: String?, body: String, timestamp: Long)

    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action == Telephony.Sms.Intents.SMS_RECEIVED_ACTION) {
            val messages = Telephony.Sms.Intents.getMessagesFromIntent(intent)

            for (sms in messages) {
                onSmsReceivedCPP(sms.originatingAddress, sms.messageBody, sms.timestampMillis)
            }
        }
    }
}