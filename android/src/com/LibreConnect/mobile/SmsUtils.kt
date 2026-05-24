package com.LibreConnect.mobile

import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.provider.ContactsContract
import android.provider.Telephony
import android.telephony.SmsManager
import android.os.Build
import android.util.Log
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.Locale
import android.content.BroadcastReceiver
import android.content.Intent
import android.net.Uri
import android.os.Handler
import android.os.Looper
import java.util.concurrent.Executors
import androidx.core.net.toUri


object SmsUtils {
    private const val tag = "SmsUtils"
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

    private fun encodeConversationSummary(name: String?, latestTimestamp: Long, latestPreview: String?): String {
        val safeName = name.orEmpty()
        val safePreview = latestPreview.orEmpty()
        return "summary|$latestTimestamp|$safeName|$safePreview"
    }

    @JvmStatic
    @SuppressLint("Range")
    fun getAllContacts(context: Context): List<Pair<String?, String?>> {
        val contactsByKey = linkedMapOf<String, Pair<String?, String?>>()
        val lastMessageTimes = mutableMapOf<String, Long>()
        val lastPreviews = mutableMapOf<String, String>()

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
                            if (normalized.isEmpty()) continue

                            contactsByKey[normalized] = Pair(cursor.getString(nameIndex), number)
                        } while (cursor.moveToNext())
                    }
                }
            } catch (e: SecurityException) {
                Log.w("SmsUtils", "getAllContacts: READ_CONTACTS query failed with SecurityException", e)
            }
        }

        if (hasPermission(context, android.Manifest.permission.READ_SMS)) {
            val uri = Telephony.Sms.CONTENT_URI
            val projection = arrayOf(Telephony.Sms.ADDRESS, Telephony.Sms.DATE, Telephony.Sms.BODY)
            val sortOrder = "${Telephony.Sms.DATE} DESC"

            try {
                context.contentResolver.query(uri, projection, null, null, sortOrder)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val addressIndex = cursor.getColumnIndex(Telephony.Sms.ADDRESS)
                        val dateIndex = cursor.getColumnIndex(Telephony.Sms.DATE)
                        val bodyIndex = cursor.getColumnIndex(Telephony.Sms.BODY)

                        do {
                            val address = cursor.getString(addressIndex)
                            val date = cursor.getLong(dateIndex)
                            val body = cursor.getString(bodyIndex).orEmpty()
                            val normalized = normalizeAddress(address)

                            if (normalized.isEmpty()) continue

                            if (!lastMessageTimes.containsKey(normalized)) {
                                lastMessageTimes[normalized] = date
                                lastPreviews[normalized] = body
                            }

                            if (!contactsByKey.containsKey(normalized)) {
                                val displayAddress = address?.trim().orEmpty()
                                val visible = if (displayAddress.isNotEmpty()) displayAddress else normalized
                                contactsByKey[normalized] = Pair(visible, visible)
                            }
                        } while (cursor.moveToNext())
                    }
                }
            } catch (e: SecurityException) {
                Log.w("SmsUtils", "getAllContacts: READ_SMS query failed with SecurityException", e)
            }
        }

        if (hasPermission(context, android.Manifest.permission.READ_SMS)) {
            val uri = Telephony.Mms.CONTENT_URI
            val projection = arrayOf(Telephony.Mms._ID, Telephony.Mms.DATE, Telephony.Mms.MESSAGE_BOX)
            val sortOrder = "${Telephony.Mms.DATE} DESC"

            try {
                context.contentResolver.query(uri, projection, null, null, sortOrder)?.use { cursor ->
                    if (cursor.moveToFirst()) {
                        val idIndex = cursor.getColumnIndex(Telephony.Mms._ID)
                        val dateIndex = cursor.getColumnIndex(Telephony.Mms.DATE)
                        val boxIndex = cursor.getColumnIndex(Telephony.Mms.MESSAGE_BOX)

                        do {
                            val mmsId = cursor.getString(idIndex)
                            val date = cursor.getLong(dateIndex) * 1000
                            val box = cursor.getInt(boxIndex)
                            val address = getMmsConversationAddress(context, mmsId, box)
                            val normalized = normalizeAddress(address)

                            if (normalized.isEmpty()) continue

                            if (!lastMessageTimes.containsKey(normalized) || date > (lastMessageTimes[normalized] ?: 0L)) {
                                val textBody = getMmsTextBody(context, mmsId).trim()
                                val attachments = getMmsAttachments(context, mmsId)
                                lastMessageTimes[normalized] = date
                                lastPreviews[normalized] = if (textBody.isNotEmpty()) {
                                    textBody
                                } else if (attachments.isNotEmpty()) {
                                    "MMS attachment"
                                } else {
                                    "MMS"
                                }
                            }

                            if (!contactsByKey.containsKey(normalized)) {
                                val visible = address?.trim().orEmpty().ifEmpty { normalized }
                                contactsByKey[normalized] = Pair(visible, visible)
                            }
                        } while (cursor.moveToNext())
                    }
                }
            } catch (_: SecurityException) {
            }
        }

        return contactsByKey.toList()
            .sortedByDescending { (normalizedKey, _) ->
                lastMessageTimes[normalizedKey] ?: 0L
            }
            .map { (normalizedKey, contact) ->
                Pair(
                    encodeConversationSummary(contact.first, lastMessageTimes[normalizedKey] ?: 0L, lastPreviews[normalizedKey]),
                    contact.second
                )
            }
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

        val messages = mutableListOf<Pair<Long, String>>()
        var threadId: Long = -1

        try {
            threadId = Telephony.Threads.getOrCreateThreadId(context, targetNumber)
        } catch (e: Exception) {
            Log.e(tag, "Failed to get thread ID", e)
        }


        if (threadId != -1L) {
            try {
                val smsUri = Telephony.Sms.CONTENT_URI
                val projection = arrayOf(
                    Telephony.Sms.BODY,
                    Telephony.Sms.DATE,
                    Telephony.Sms.TYPE
                )
                context.contentResolver.query(
                    smsUri, projection, "thread_id = ?", arrayOf(threadId.toString()), null
                )?.use { cursor ->
                    val bodyIndex = cursor.getColumnIndex(Telephony.Sms.BODY)
                    val dateIndex = cursor.getColumnIndex(Telephony.Sms.DATE)
                    val typeIndex = cursor.getColumnIndex(Telephony.Sms.TYPE)

                    while (cursor.moveToNext()) {
                        val body = cursor.getString(bodyIndex) ?: ""
                        val timestamp = cursor.getLong(dateIndex)
                        val type = cursor.getInt(typeIndex)
                        messages.add(Pair(timestamp, "v2|$type|$timestamp|$body"))
                    }
                }
            } catch (e: Exception) {
                Log.e(tag, "Failed to query SMS by thread", e)
            }

            try {
                val mmsUri = Telephony.Mms.CONTENT_URI
                val projection = arrayOf(
                    Telephony.Mms._ID,
                    Telephony.Mms.DATE,
                    Telephony.Mms.MESSAGE_BOX
                )
                context.contentResolver.query(
                    mmsUri, projection, "thread_id = ?", arrayOf(threadId.toString()), null
                )?.use { cursor ->
                    val idIndex = cursor.getColumnIndex(Telephony.Mms._ID)
                    val dateIndex = cursor.getColumnIndex(Telephony.Mms.DATE)
                    val msgBoxIndex = cursor.getColumnIndex(Telephony.Mms.MESSAGE_BOX)

                    while (cursor.moveToNext()) {
                        val mmsId = cursor.getString(idIndex)
                        // MMS timestamps are in seconds; convert to ms to match SMS
                        val timestamp = cursor.getLong(dateIndex) * 1000
                        val type = cursor.getInt(msgBoxIndex)

                        val body = getMmsTextBody(context, mmsId)
                        val attachments = getMmsAttachments(context, mmsId)

                        if (attachments.isEmpty()) {
                            messages.add(Pair(timestamp, "v2|$type|$timestamp|$body"))
                        } else {
                            val attachStr = attachments.joinToString(";")
                            messages.add(Pair(timestamp, "mms|$type|$timestamp|$body|$attachStr"))
                        }
                    }
                }
            } catch (e: Exception) {
                Log.e(tag, "Failed to query MMS by thread", e)
            }
        } else {
            val uri = Telephony.Sms.CONTENT_URI
            val projection = arrayOf(
                Telephony.Sms.ADDRESS,
                Telephony.Sms.BODY,
                Telephony.Sms.DATE,
                Telephony.Sms.TYPE
            )

            try {
                context.contentResolver.query(uri, projection, null, null, null)?.use { cursor ->
                    val addressIndex = cursor.getColumnIndex(Telephony.Sms.ADDRESS)
                    val bodyIndex = cursor.getColumnIndex(Telephony.Sms.BODY)
                    val dateIndex = cursor.getColumnIndex(Telephony.Sms.DATE)
                    val typeIndex = cursor.getColumnIndex(Telephony.Sms.TYPE)

                    while (cursor.moveToNext()) {
                        val address = cursor.getString(addressIndex)
                        if (normalizeAddress(address) != normalizedTarget) continue

                        val body = cursor.getString(bodyIndex) ?: ""
                        val timestamp = cursor.getLong(dateIndex)
                        val type = cursor.getInt(typeIndex)
                        messages.add(Pair(timestamp, "v2|$type|$timestamp|$body"))
                    }
                }
            } catch (e: Exception) {
                Log.e(tag, "Failed to query SMS fallback", e)
            }
        }

        return messages.sortedByDescending { it.first }.map { it.second }
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

    @JvmStatic
    fun getMmsSender(context: Context, mmsId: String): String? {
        val uri = "content://mms/$mmsId/addr".toUri()
        val projection = arrayOf("address")

        try {
            context.contentResolver.query(
                uri,
                projection,
                "type=137",
                null,
                null
            )?.use { cursor ->
                if (cursor.moveToFirst()) {
                    val address = cursor.getString(cursor.getColumnIndexOrThrow("address"))
                    if (!address.isNullOrBlank() && address != "insert-address-token") {
                        return address
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to resolve MMS sender", e)
        }
        return null
    }

    @JvmStatic
    fun getMmsConversationAddress(context: Context, mmsId: String, messageBox: Int): String? {
        val uri = "content://mms/$mmsId/addr".toUri()
        val projection = arrayOf("address")
        val addressType = if (messageBox == Telephony.Mms.MESSAGE_BOX_SENT) 151 else 137

        try {
            context.contentResolver.query(
                uri,
                projection,
                "type=$addressType",
                null,
                null
            )?.use { cursor ->
                while (cursor.moveToNext()) {
                    val address = cursor.getString(cursor.getColumnIndexOrThrow("address"))
                    if (!address.isNullOrBlank() && address != "insert-address-token") {
                        return address
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to resolve MMS conversation address", e)
        }

        return getMmsSender(context, mmsId)
    }

    @JvmStatic
    fun getMmsTextBody(context: Context, mmsId: String): String {
        val uri = "content://mms/part".toUri()
        val projection = arrayOf("_id", "ct", "text")
        val bodyBuilder = StringBuilder()

        try {
            context.contentResolver.query(
                uri,
                projection,
                "mid=$mmsId",
                null,
                null
            )?.use { cursor ->
                while (cursor.moveToNext()) {
                    val partId = cursor.getString(cursor.getColumnIndexOrThrow("_id"))
                    val contentType = cursor.getString(cursor.getColumnIndexOrThrow("ct"))

                    if ("text/plain" == contentType) {
                        val text = cursor.getString(cursor.getColumnIndexOrThrow("text"))
                        if (text != null) {
                            bodyBuilder.append(text)
                        } else {
                            bodyBuilder.append(readTextFromPartUri(context, partId))
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Log.e(tag, "Failed to resolve MMS body parts", e)
        }
        return bodyBuilder.toString()
    }

    @JvmStatic
    private fun readTextFromPartUri(context: Context, partId: String): String {
        val partUri = "content://mms/part/$partId".toUri()
        val sb = StringBuilder()
        try {
            context.contentResolver.openInputStream(partUri)?.use { isStream ->
                InputStreamReader(isStream, "UTF-8").use { isr ->
                    BufferedReader(isr).use { reader ->
                        var line = reader.readLine()
                        while (line != null) {
                            sb.append(line)
                            line = reader.readLine()
                        }
                    }
                }
            }
        } catch (_: Exception) {}
        return sb.toString()
    }

    @JvmStatic
    fun getMmsAttachments(context: Context, mmsId: String): List<String> {
        val attachmentUris = mutableListOf<String>()
        val uri = "content://mms/part".toUri()
        val projection = arrayOf("_id", "ct")

        try {
            context.contentResolver.query(
                uri,
                projection,
                "mid=$mmsId",
                null,
                null
            )?.use { cursor ->
                while (cursor.moveToNext()) {
                    val partId = cursor.getString(cursor.getColumnIndexOrThrow("_id"))
                    val contentType = cursor.getString(cursor.getColumnIndexOrThrow("ct"))

                    if (contentType != "text/plain" && contentType != "application/smil") {
                        val partUriString = "content://mms/part/$partId"
                        attachmentUris.add(partUriString)
                    }
                }
            }

        } catch (e: Exception) {
            Log.e(tag, "Failed to extract MMS attachments", e)
        }

        return attachmentUris
    }

    @JvmStatic
    fun saveAttachmentToCache(context: Context, partUriString: String): String? {
        val uri = partUriString.toUri()
        val contentResolver = context.contentResolver

        val mimeType = contentResolver.getType(uri) ?: "application/octet-stream"
        val extension = when {
            mimeType.startsWith("image/jpeg") -> ".jpg"
            mimeType.startsWith("image/png") -> ".png"
            mimeType.startsWith("image/gif") -> ".gif"
            mimeType.startsWith("video/mp4") -> ".mp4"
            mimeType.startsWith("audio/") -> ".mp3"
            else -> ".bin"
        }

        return try {
            val inputStream = contentResolver.openInputStream(uri) ?: return null

            val fileName = "mms_attach_${System.currentTimeMillis()}_${uri.lastPathSegment}$extension"
            val file = java.io.File(context.cacheDir, fileName)

            inputStream.use { input ->
                file.outputStream().use { output ->
                    input.copyTo(output)
                }
            }

            file.absolutePath
        } catch (e: Exception) {
            Log.e(tag, "Failed to save attachment to cache", e)
            null
        }
    }
}
