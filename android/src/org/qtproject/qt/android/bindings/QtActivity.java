package org.qtproject.qt.android.bindings;

import android.content.pm.PackageManager;
import android.os.Build;

import org.qtproject.qt.android.QtActivityBase;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public class QtActivity extends QtActivityBase {
    private static final Object PERMISSION_LOCK = new Object();
    private static final AtomicInteger NEXT_PERMISSION_REQUEST_CODE = new AtomicInteger(1000);

    private static volatile QtActivity instance;
    private static PendingPermissionRequest pendingPermissionRequest;

    private static final class PendingPermissionRequest {
        final String permission;
        final int requestCode;
        final CountDownLatch latch = new CountDownLatch(1);
        volatile boolean granted;

        PendingPermissionRequest(String permission, int requestCode) {
            this.permission = permission;
            this.requestCode = requestCode;
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        instance = this;
    }

    @Override
    protected void onDestroy() {
        if (instance == this) {
            instance = null;
        }
        super.onDestroy();
    }

    public static int checkPermission(String permission) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return PackageManager.PERMISSION_GRANTED;
        }

        final QtActivity activity = instance;
        if (activity == null || permission == null || permission.isEmpty()) {
            return PackageManager.PERMISSION_DENIED;
        }

        return activity.checkSelfPermission(permission);
    }

    public static boolean requestPermissionBlocking(String permission, int timeoutMs) {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true;
        }

        final QtActivity activity = instance;
        if (activity == null || permission == null || permission.isEmpty()) {
            return false;
        }

        if (activity.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED) {
            return true;
        }

        final PendingPermissionRequest request;
        synchronized (PERMISSION_LOCK) {
            if (pendingPermissionRequest != null) {
                return false;
            }

            request = new PendingPermissionRequest(permission, NEXT_PERMISSION_REQUEST_CODE.getAndIncrement());
            pendingPermissionRequest = request;
        }

        activity.runOnUiThread(() -> activity.requestPermissions(new String[]{permission}, request.requestCode));

        try {
            if (!request.latch.await(timeoutMs, TimeUnit.MILLISECONDS)) {
                synchronized (PERMISSION_LOCK) {
                    if (pendingPermissionRequest == request) {
                        pendingPermissionRequest = null;
                    }
                }
                return activity.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
            }
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            synchronized (PERMISSION_LOCK) {
                if (pendingPermissionRequest == request) {
                    pendingPermissionRequest = null;
                }
            }
            return false;
        }

        return request.granted || activity.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);

        final PendingPermissionRequest request;
        synchronized (PERMISSION_LOCK) {
            if (pendingPermissionRequest == null || pendingPermissionRequest.requestCode != requestCode) {
                return;
            }

            request = pendingPermissionRequest;
            pendingPermissionRequest = null;
        }

        final boolean permissionMatches = permissions != null
                && permissions.length > 0
                && request.permission.equals(permissions[0]);
        request.granted = permissionMatches
                && grantResults != null
                && grantResults.length > 0
                && grantResults[0] == PackageManager.PERMISSION_GRANTED;
        request.latch.countDown();
    }
}
