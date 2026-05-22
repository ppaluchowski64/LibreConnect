package com.LibreConnect.mobile

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

object MicrophoneReceiver {
    private const val TAG = "LibreConnectNative"
    private const val SAMPLE_RATE = 48000
    private const val CHANNELS = AudioFormat.CHANNEL_IN_STEREO
    private const val ENCODING = AudioFormat.ENCODING_PCM_16BIT
    
    // 20ms Frame = 48000 * 0.02 * 2 (stereo) * 2 (16bit) = 3840 bytes
    private const val FRAME_SIZE_BYTES = 3840 

    private var audioRecord: AudioRecord? = null
    private val isRunning = AtomicBoolean(false)
    private var captureThread: Thread? = null
    private val lock = Any()

    @SuppressLint("MissingPermission")
    fun start(): Boolean {
        synchronized(lock) {
            Log.d(TAG, "MicrophoneReceiver: start() called")
            if (isRunning.get()) {
                Log.d(TAG, "MicrophoneReceiver: already running")
                return true
            }
            isRunning.set(true)
        }

        // Notify MainService to include microphone foreground service type
        MainService.setMicrophoneRequested(true)

        captureThread = Thread {
            Log.d(TAG, "MicrophoneReceiver: Capture thread started")
            
            val minBufferSize = AudioRecord.getMinBufferSize(SAMPLE_RATE, CHANNELS, ENCODING)
            val bufferSize = maxOf(minBufferSize, FRAME_SIZE_BYTES * 4)
            Log.d(TAG, "MicrophoneReceiver: minBufferSize=$minBufferSize, bufferSize=$bufferSize")

            synchronized(lock) {
                if (!isRunning.get()) {
                    MainService.setMicrophoneRequested(false)
                    return@Thread
                }
                try {
                    audioRecord = AudioRecord(
                        MediaRecorder.AudioSource.MIC,
                        SAMPLE_RATE, CHANNELS, ENCODING, bufferSize
                    )
                    Log.d(TAG, "MicrophoneReceiver: AudioRecord created")
                } catch (e: Exception) {
                    Log.e(TAG, "MicrophoneReceiver: Failed to create AudioRecord", e)
                    isRunning.set(false)
                    MainService.setMicrophoneRequested(false)
                    return@Thread
                }

                if (audioRecord?.state != AudioRecord.STATE_INITIALIZED) {
                    Log.e(TAG, "MicrophoneReceiver: AudioRecord not initialized")
                    audioRecord?.release()
                    audioRecord = null
                    isRunning.set(false)
                    MainService.setMicrophoneRequested(false)
                    return@Thread
                }

                try {
                    audioRecord?.startRecording()
                    Log.d(TAG, "MicrophoneReceiver: startRecording() successful")
                } catch (e: Exception) {
                    Log.e(TAG, "MicrophoneReceiver: startRecording() failed", e)
                    audioRecord?.release()
                    audioRecord = null
                    isRunning.set(false)
                    MainService.setMicrophoneRequested(false)
                    return@Thread
                }
            }

            val buffer = ByteArray(FRAME_SIZE_BYTES)
            try {
                while (isRunning.get()) {
                    val record = audioRecord
                    val read = record?.read(buffer, 0, FRAME_SIZE_BYTES) ?: -1
                    if (read == FRAME_SIZE_BYTES) {
                        MainService.onAudioCaptured(buffer.clone())
                    } else if (read < 0) {
                        Log.e(TAG, "MicrophoneReceiver: Error reading from AudioRecord: $read")
                        break
                    }
                }
            } catch (e: Exception) {
                Log.e(TAG, "MicrophoneReceiver: Exception in capture thread", e)
            } finally {
                synchronized(lock) {
                    try {
                        audioRecord?.stop()
                    } catch (_: Exception) {}
                    audioRecord?.release()
                    audioRecord = null
                    isRunning.set(false)
                }
                MainService.setMicrophoneRequested(false)
                Log.d(TAG, "MicrophoneReceiver: Capture thread exiting")
            }
        }.apply { 
            name = "MicrophoneCaptureThread"
            start() 
        }

        return true
    }

    fun stop() {
        Log.d(TAG, "MicrophoneReceiver: stop() called")
        isRunning.set(false)
        
        synchronized(lock) {
            try {
                audioRecord?.stop()
                Log.d(TAG, "MicrophoneReceiver: audioRecord.stop() successful")
            } catch (e: Exception) {
                Log.e(TAG, "MicrophoneReceiver: audioRecord.stop() failed", e)
            }
        }

        try {
            captureThread?.join(1000)
        } catch (e: InterruptedException) {
            Log.e(TAG, "MicrophoneReceiver: Interrupted while waiting for capture thread to exit", e)
        }
        captureThread = null
        Log.d(TAG, "MicrophoneReceiver: stopped")
    }
}
