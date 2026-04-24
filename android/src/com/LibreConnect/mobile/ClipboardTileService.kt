package com.LibreConnect.mobile

import android.os.Build
import android.service.quicksettings.Tile
import android.service.quicksettings.TileService
import android.util.Log

class ClipboardTileService : TileService() {
    override fun onTileAdded() {
        super.onTileAdded()
        Log.i(TAG, "Clipboard tile added")
        refreshTile()
    }

    override fun onStartListening() {
        super.onStartListening()
        refreshTile()
    }

    override fun onClick() {
        super.onClick()
        Log.i(TAG, "Clipboard tile clicked")
        refreshTile()
        unlockAndRun {
            startActivityAndCollapse(ClipboardActionActivity.createLaunchIntent(this))
        }
    }

    private fun refreshTile() {
        val tile = qsTile ?: return
        tile.state = Tile.STATE_ACTIVE
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            tile.subtitle = "Send clipboard"
        }
        tile.updateTile()
    }

    companion object {
        private const val TAG = "ClipboardTileService"
    }
}
