package com.LibreConnect.mobile

import android.content.Context
import android.media.AudioManager
import android.view.KeyEvent
import kotlin.math.roundToInt

class MediaRemoteBridge {
    companion object {
        @JvmStatic
        fun sendMediaKey(context: Context, keyCode: Int) {
            val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_DOWN, keyCode))
            audioManager.dispatchMediaKeyEvent(KeyEvent(KeyEvent.ACTION_UP, keyCode))
        }

        @JvmStatic
        fun getVolume(context: Context): Int {
            val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            val current = audioManager.getStreamVolume(AudioManager.STREAM_MUSIC)
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)

            if (max == 0)
                return 0

            return ((current.toDouble() * 100.0) / max.toDouble()).roundToInt()
        }

        @JvmStatic
        fun setVolume(context: Context, percentage: Int) {
            val audioManager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
            val max = audioManager.getStreamMaxVolume(AudioManager.STREAM_MUSIC)
            val newVolume = ((percentage.toDouble() * max.toDouble()) / 100.0).roundToInt()

            audioManager.setStreamVolume(AudioManager.STREAM_MUSIC, newVolume, AudioManager.FLAG_SHOW_UI)
        }
    }
}
