package com.LibreConnect.mobile

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.os.Handler
import android.os.Looper
import android.provider.Telephony
import android.util.Log
import java.util.concurrent.Executors
import androidx.core.net.toUri

class SmsReceiver : BroadcastReceiver() {
    private val tag = "SmsReceiver"
    private val executor = Executors.newSingleThreadExecutor()
    external fun onSmsReceivedCPP(sender: String?, body: String, timestamp: Long)
    external fun onMmsReceivedCPP(sender: String?, body: String, timestamp: Long, attachmentUris: Array<String>)

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
        } else if (intent.action == Telephony.Sms.Intents.WAP_PUSH_RECEIVED_ACTION) {
            val mimeType = intent.type
            if (mimeType == "application/vnd.wap.mms-message") {
                val pendingResult = goAsync()

                Handler(Looper.getMainLooper()).postDelayed({
                    executor.execute {
                        try {
                            fetchAndProcessLatestMms(context)
                        } catch (e: Exception) {
                            Log.e(tag, "Error fetching latest MMS details", e)
                        } finally {
                            pendingResult.finish()
                        }
                    }
                }, 4000)
            }
        }
    }

    private fun fetchAndProcessLatestMms(context: Context) {
        val mmsUri = "content://mms".toUri()
        val projection = arrayOf("_id", "date", "msg_box")
        val contentResolver = context.contentResolver

        contentResolver.query(
            mmsUri,
            projection,
            "msg_box = 1",
            null,
            "date DESC LIMIT 1"
        )?.use { cursor ->
            if (cursor.moveToFirst()) {
                val mmsId = cursor.getString(cursor.getColumnIndexOrThrow("_id"))
                val rawDate = cursor.getLong(cursor.getColumnIndexOrThrow("date"))
                val timestamp = rawDate * 1000

                val sender = SmsUtils.getMmsSender(context, mmsId)
                val textBody = SmsUtils.getMmsTextBody(context, mmsId)
                val attachments = SmsUtils.getMmsAttachments(context, mmsId)

                Log.d(tag, "Fetched MMS ID: $mmsId | Sender: $sender | Attachments: ${attachments.size}")

                onMmsReceivedCPP(sender, textBody, timestamp, attachments.toTypedArray())
            }
        }
    }
}
