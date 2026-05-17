package org.qtproject.qt.android.bindings;

import com.LibreConnect.mobile.AndroidPermissionBridge;

import org.qtproject.qt.android.QtActivityBase;

public class QtActivity extends QtActivityBase {
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
}
