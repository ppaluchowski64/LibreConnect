package com.LibreConnect.mobile

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import java.util.concurrent.atomic.AtomicBoolean

object MicrophoneReceiver {
    private const val SAMPLE_RATE = 48000
    private const val CHANNELS = AudioFormat.CHANNEL_IN_STEREO
    private const val ENCODING = AudioFormat.ENCODING_PCM_16BIT
    
    // 20ms Frame = 48000 * 0.02 * 2 (stereo) * 2 (16bit) = 3840 bytes
    private const val FRAME_SIZE_BYTES = 3840 

    private var audioRecord: AudioRecord? = null
    private val isRunning = AtomicBoolean(false)
    private var captureThread: Thread? = null

    @SuppressLint("MissingPermission")
    fun start(onSamplesCaptured: (ByteArray) -> Unit): Boolean {
        if (isRunning.get()) return true

        val minBufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNELS, ENCODING)
        val bufferSize = maxOf(minBufferSize, FRAME_SIZE_BYTES * 4)

        try {
            audioRecord = AudioRecord(
                MediaRecorder.AudioSource.MIC,
                SAMPLE_RATE, CHANNELS, ENCODING, bufferSize
            )
        } catch (e: Exception) {
            return false
        }

        if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
            audioRecord?.release()
            audioRecord = null
            return false
        }

        isRunning.set(true)
        audioRecord?.startRecording()

        captureThread = Thread {
            val buffer = ByteArray(FRAME_SIZE_BYTES)
            while (isRunning.get()) {
                val read = audioRecord?.read(buffer, 0, FRAME_SIZE_BYTES) ?: -1
                if (read == FRAME_SIZE_BYTES) {
                    onSamplesCaptured(buffer.clone())
                }
            }
        }.apply { 
            name = "MicrophoneCaptureThread"
            start() 
        }

        return true
    }

    fun stop() {
        isRunning.set(false)
        captureThread?.join(500)
        try {
            audioRecord?.stop()
        } catch (e: Exception) {}
        audioRecord?.release()
        audioRecord = null
        captureThread = null
    }
}
