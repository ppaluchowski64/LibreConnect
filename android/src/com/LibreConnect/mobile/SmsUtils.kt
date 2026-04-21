package com.LibreConnect.mobile

import android.annotation.SuppressLint
import android.content.Context
import android.provider.ContactsContract
import android.provider.Telephony
import android.telephony.SmsManager
import android.os.Build

object SmsUtils {
    @JvmStatic
    @SuppressLint("Range")
    fun getAllContacts(context: Context): List<Pair<String?, String?>> {
        val contactsList = mutableListOf<Pair<String?, String?>>()
        val uri = ContactsContract.CommonDataKinds.Phone.CONTENT_URI
        val projection = arrayOf(
            ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
            ContactsContract.CommonDataKinds.Phone.NUMBER
        )

        context.contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val nameIndex = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME)
                val numberIndex = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER)

                do {
                    contactsList.add(Pair(cursor.getString(nameIndex), cursor.getString(numberIndex)))
                } while (cursor.moveToNext())
            }
        }
        return contactsList
    }

    @JvmStatic
    @SuppressLint("Range")
    fun getMessagesFromNumber(context: Context, targetNumber: String): List<String> {
        val messages = mutableListOf<String>()
        val uri = Telephony.Sms.CONTENT_URI
        val projection = arrayOf(
            Telephony.Sms.BODY,
            Telephony.Sms.DATE,
            Telephony.Sms.TYPE
        )

        val selection = "${Telephony.Sms.ADDRESS} = ?"
        val selectionArgs = arrayOf(targetNumber)
        val sortOrder = "${Telephony.Sms.DATE} DESC"

        context.contentResolver.query(uri, projection, selection, selectionArgs, sortOrder)?.use { cursor ->
            if (cursor.moveToFirst()) {
                val bodyIndex = cursor.getColumnIndex(Telephony.Sms.BODY)
                val typeIndex = cursor.getColumnIndex(Telephony.Sms.TYPE)

                do {
                    val body = cursor.getString(bodyIndex) ?: ""
                    val type = cursor.getInt(typeIndex)
                    messages.add("$type$body")
                } while (cursor.moveToNext())
            }
        }

        return messages
    }

    @JvmStatic
    fun sendSms(context: Context, targetNumber: String, message: String): Boolean {
        return try {
            val smsManager: SmsManager = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                context.getSystemService(SmsManager::class.java)
            } else {
                @Suppress("DEPRECATION")
                SmsManager.getDefault()
            }
            smsManager.sendTextMessage(targetNumber, null, message, null, null)
            true
        } catch (e: Exception) {
            false
        }
    }
}
