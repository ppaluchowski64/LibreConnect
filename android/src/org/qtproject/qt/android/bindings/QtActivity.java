package org.qtproject.qt.android.bindings;

import android.content.Intent;
import android.os.Bundle;

import com.LibreConnect.mobile.AndroidPermissionBridge;

import org.qtproject.qt.android.QtActivityBase;

public class QtActivity extends QtActivityBase {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    private void handleIntent(Intent intent) {
        if (intent != null && intent.hasExtra("NAVIGATE_TO")) {
            String page = intent.getStringExtra("NAVIGATE_TO");
            if ("media_remote".equals(page)) {
                nativeNavigateToMediaRemote();
            }
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        AndroidPermissionBridge.onActivityResumed(this);
    }

    @Override
    protected void onDestroy() {
        AndroidPermissionBridge.onActivityDestroyed(this);
        super.onDestroy();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        AndroidPermissionBridge.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    private static native void nativeNavigateToMediaRemote();
}
