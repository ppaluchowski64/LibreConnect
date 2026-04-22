package com.LibreConnect.mobile

import android.service.quicksettings.TileService
import android.util.Log

class ClipboardTileService : TileService() {

    override fun onClick() {
        super.onClick()
        Log.i(TAG, "Clipboard tile clicked")
        startActivityAndCollapse(ClipboardActionActivity.createLaunchIntent(this))
    }

    companion object {
        private const val TAG = "ClipboardTileService"
    }
}
