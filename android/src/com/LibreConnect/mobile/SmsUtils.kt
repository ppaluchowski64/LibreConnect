package com.LibreConnect.mobile

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.provider.ContactsContract
import android.provider.Telephony
import android.telephony.SmsManager
import android.os.Build
import androidx.core.content.ContextCompat
import java.util.Locale

object SmsUtils {
    private fun hasPermission(context: Context, permission: String): Boolean {
        return ContextCompat.checkSelfPermission(context, permission) == PackageManager.PERMISSION_GRANTED
    }

    private fun normalizeAddress(raw: String?): String {
        if (raw.isNullOrBlank()) {
            return ""
        }

        val trimmed = raw.trim()
        val hasLetters = trimmed.any { it.isLetter() }
        val builder = StringBuilder(trimmed.length)
        var plusUsed = false
        for (ch in trimmed) {
            when {
                ch.isDigit() -> builder.append(ch)
                ch == '+' && !plusUsed && builder.isEmpty() -> {
                    builder.append(ch)
                    plusUsed = true
                }
            }
        }

        if (!hasLetters && builder.isNotEmpty()) {
            return builder.toString()
        }

        return trimmed
            .replace(Regex("\\s+"), " ")
            .lowercase(Locale.ROOT)
    }

    @JvmStatic
    @SuppressLint("Range")
    fun getAllContacts(context: Context): List<Pair<String?, String?>> {
        val contactsByKey = linkedMapOf<String, Pair<String?, String?>>()

        if (hasPermission(context, android.Manifest.permission.READ_CONTACTS)) {
            val uri = ContactsContract.CommonDataKinds.Phone.CONTENT_URI
            val projection = arrayOf(
                ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME,
                ContactsContract.CommonDataKinds.Phone.NUMBER
            )

            try {
                context.contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val nameIndex = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.DISPLAY_NAME)
                        val numberIndex = cursor.getColumnIndex(ContactsContract.CommonDataKinds.Phone.NUMBER)

                        do {
                            val number = cursor.getString(numberIndex)
                            val normalized = normalizeAddress(number)
                            if (normalized.isEmpty()) {
                                continue
                            }

                            contactsByKey[normalized] = Pair(cursor.getString(nameIndex), number)
                        } while (cursor.moveToNext())
                    }
                }
            } catch (_: SecurityException) {
                // Ignore and continue with SMS conversation fallback.
            }
        }

        if (hasPermission(context, android.Manifest.permission.READ_SMS)) {
            val uri = Telephony.Sms.CONTENT_URI
            val projection = arrayOf(Telephony.Sms.ADDRESS)
            val sortOrder = "${Telephony.Sms.DATE} DESC"

            try {
                context.contentResolver.query(uri, projection, null, null, sortOrder)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val addressIndex = cursor.getColumnIndex(Telephony.Sms.ADDRESS)
                        do {
                            val address = cursor.getString(addressIndex)
                            val normalized = normalizeAddress(address)
                            if (normalized.isEmpty() || contactsByKey.containsKey(normalized)) {
                                continue
                            }

                            val displayAddress = address?.trim().orEmpty()
                            val visible = if (displayAddress.isNotEmpty()) displayAddress else normalized
                            contactsByKey[normalized] = Pair(visible, visible)
                        } while (cursor.moveToNext())
                    }
                }
            } catch (_: SecurityException) {
                // Best effort only.
            }
        }

        return contactsByKey.values.toList()
    }

    @JvmStatic
    @SuppressLint("Range")
    fun getMessagesFromNumber(context: Context, targetNumber: String): List<String> {
        if (!hasPermission(context, android.Manifest.permission.READ_SMS)) {
            return emptyList()
        }

        val normalizedTarget = normalizeAddress(targetNumber)
        if (normalizedTarget.isEmpty()) {
            return emptyList()
        }

        val messages = mutableListOf<String>()
        val uri = Telephony.Sms.CONTENT_URI
        val projection = arrayOf(
            Telephony.Sms.ADDRESS,
            Telephony.Sms.BODY,
            Telephony.Sms.DATE,
            Telephony.Sms.TYPE
        )

        val sortOrder = "${Telephony.Sms.DATE} DESC"

        try {
            context.contentResolver.query(uri, projection, null, null, sortOrder)?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val addressIndex = cursor.getColumnIndex(Telephony.Sms.ADDRESS)
                    val bodyIndex = cursor.getColumnIndex(Telephony.Sms.BODY)
                    val dateIndex = cursor.getColumnIndex(Telephony.Sms.DATE)
                    val typeIndex = cursor.getColumnIndex(Telephony.Sms.TYPE)

                    do {
                        val address = cursor.getString(addressIndex)
                        if (normalizeAddress(address) != normalizedTarget) {
                            continue
                        }

                        val body = cursor.getString(bodyIndex) ?: ""
                        val timestamp = cursor.getLong(dateIndex)
                        val type = cursor.getInt(typeIndex)
                        messages.add("v2|$type|$timestamp|$body")
                    } while (cursor.moveToNext())
                }
            }
        } catch (_: SecurityException) {
            return emptyList()
        }

        return messages
    }

    @JvmStatic
    fun sendSms(context: Context, targetNumber: String, message: String): Boolean {
        if (!hasPermission(context, android.Manifest.permission.SEND_SMS)) {
            return false
        }

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
