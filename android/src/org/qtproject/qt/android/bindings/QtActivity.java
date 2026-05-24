package org.qtproject.qt.android.bindings;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

import java.lang.ref.WeakReference;

import com.LibreConnect.mobile.AndroidPermissionBridge;
import com.LibreConnect.mobile.MainService;

import org.qtproject.qt.android.QtActivityBase;

public class QtActivity extends QtActivityBase {
    private static final String TAG = "LibreConnectNative";
    private static WeakReference<QtActivity> activeActivity = new WeakReference<>(null);

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        Log.d(TAG, "QtActivity.onCreate: before super");
        activeActivity = new WeakReference<>(this);
        startBackendService();
        super.onCreate(savedInstanceState);
        Log.d(TAG, "QtActivity.onCreate: after super");
        handleIntent(getIntent());
        Log.d(TAG, "QtActivity.onCreate: done");
    }

    @Override
    protected void onStart() {
        Log.d(TAG, "QtActivity.onStart: before super");
        super.onStart();
        Log.d(TAG, "QtActivity.onStart: after super");
    }

    @Override
    protected void onNewIntent(Intent intent) {
        Log.d(TAG, "QtActivity.onNewIntent: before super");
        super.onNewIntent(intent);
        Log.d(TAG, "QtActivity.onNewIntent: after super");
        handleIntent(intent);
        Log.d(TAG, "QtActivity.onNewIntent: done");
    }

    private void handleIntent(Intent intent) {
        if (intent != null && intent.hasExtra("NAVIGATE_TO")) {
            String page = intent.getStringExtra("NAVIGATE_TO");
            if ("media_remote".equals(page)) {
                nativeNavigateToMediaRemote();
            }
        }

        if (intent == null || !intent.hasExtra(MainService.EXTRA_BACKEND_EVENT)) {
            return;
        }

        String event = intent.getStringExtra(MainService.EXTRA_BACKEND_EVENT);
        try {
            if (MainService.BACKEND_EVENT_CONNECTION_PENDING.equals(event)) {
                nativeBackendConnectionPending(
                    intent.getStringExtra(MainService.EXTRA_DEVICE_ID),
                    intent.getStringExtra(MainService.EXTRA_DEVICE_NAME),
                    intent.getIntExtra(MainService.EXTRA_CONNECTION_MODE, -1),
                    intent.getStringExtra(MainService.EXTRA_PAIRING_CODE)
                );
            } else if (MainService.BACKEND_EVENT_CONNECTION_APPROVAL.equals(event)) {
                nativeBackendConnectionApprovalRequested(
                    intent.getStringExtra(MainService.EXTRA_DEVICE_ID),
                    intent.getStringExtra(MainService.EXTRA_DEVICE_NAME)
                );
            }
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "Backend event native handler unavailable", e);
        }
    }

    private void startBackendService() {
        Intent serviceIntent = new Intent(this, MainService.class);
        serviceIntent.setAction(MainService.ACTION_START_BACKEND);
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                startForegroundService(serviceIntent);
            } else {
                startService(serviceIntent);
            }
        } catch (Throwable t) {
            Log.w(TAG, "Failed to start backend service from activity", t);
        }
    }

    @Override
    protected void onResume() {
        Log.d(TAG, "QtActivity.onResume: before super");
        activeActivity = new WeakReference<>(this);
        super.onResume();
        Log.d(TAG, "QtActivity.onResume: after super");
        AndroidPermissionBridge.onActivityResumed(this);
        Log.d(TAG, "QtActivity.onResume: done");
    }

    @Override
    protected void onPause() {
        Log.d(TAG, "QtActivity.onPause: before super");
        super.onPause();
        Log.d(TAG, "QtActivity.onPause: after super");
    }

    @Override
    protected void onStop() {
        Log.d(TAG, "QtActivity.onStop: before super");
        super.onStop();
        Log.d(TAG, "QtActivity.onStop: after super");
    }

    @Override
    protected void onDestroy() {
        Log.d(TAG, "QtActivity.onDestroy: before bridge cleanup");
        QtActivity activity = activeActivity.get();
        if (activity == this) {
            activeActivity = new WeakReference<>(null);
        }
        AndroidPermissionBridge.onActivityDestroyed(this);
        try {
            nativeActivityDestroying();
        } catch (UnsatisfiedLinkError e) {
            Log.w(TAG, "nativeActivityDestroying unavailable", e);
        }
        Log.d(TAG, "QtActivity.onDestroy: before super");
        super.onDestroy();
        Log.d(TAG, "QtActivity.onDestroy: after super");
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        Log.d(TAG, "QtActivity.onRequestPermissionsResult: before super");
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        Log.d(TAG, "QtActivity.onRequestPermissionsResult: after super");
        AndroidPermissionBridge.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    private static native void nativeNavigateToMediaRemote();
    private static native void nativeActivityDestroying();
    private static native void nativeBackendConnectionPending(String deviceId, String deviceName, int connectionMode, String pairingCode);
    private static native void nativeBackendConnectionApprovalRequested(String deviceId, String deviceName);

    static void quitFromServiceNotification() {
        QtActivity activity = activeActivity.get();
        if (activity != null) {
            activity.runOnUiThread(() -> {
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
                    activity.finishAndRemoveTask();
                } else {
                    activity.finish();
                }
                Process.killProcess(Process.myPid());
            });
            return;
        }

        Process.killProcess(Process.myPid());
    }
}
