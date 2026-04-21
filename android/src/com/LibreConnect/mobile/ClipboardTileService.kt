package com.LibreConnect.mobile

import android.content.Intent
import android.service.quicksettings.TileService
import android.util.Log

class ClipboardTileService : TileService() {

    override fun onClick() {
        super.onClick()
        Log.i(TAG, "Clipboard tile clicked")

        val intent = Intent(this, ClipboardActionActivity::class.java).apply {
            addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
        }

        startActivityAndCollapse(intent)
    }

    companion object {
        private const val TAG = "ClipboardTileService"
    }
}
