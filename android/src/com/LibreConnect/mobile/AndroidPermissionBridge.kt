package com.LibreConnect.mobile

import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.Build
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger

object AndroidPermissionBridge {
    private val permissionLock = Any()
    private val nextPermissionRequestCode = AtomicInteger(1000)

    @Volatile
    private var activeActivity: Activity? = null

    @Volatile
    private var pendingPermissionRequest: PendingPermissionRequest? = null

    private class PendingPermissionRequest(
        val permission: String,
        val requestCode: Int
    ) {
        val latch = CountDownLatch(1)

        @Volatile
        var granted = false
    }

    @JvmStatic
    fun onActivityResumed(activity: Activity) {
        activeActivity = activity
    }

    @JvmStatic
    fun onActivityDestroyed(activity: Activity) {
        if (activeActivity === activity) {
            activeActivity = null
        }
    }

    @JvmStatic
    fun checkPermission(permission: String?): Int {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return PackageManager.PERMISSION_GRANTED
        }

        if (permission.isNullOrEmpty()) {
            return PackageManager.PERMISSION_DENIED
        }

        val context = resolvePermissionContext() ?: return PackageManager.PERMISSION_DENIED
        return ContextCompat.checkSelfPermission(context, permission)
    }

    @JvmStatic
    fun shouldShowRequestPermissionRationale(permission: String?): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M || permission.isNullOrEmpty()) {
            return false
        }

        val activity = activeActivity ?: return false
        return ActivityCompat.shouldShowRequestPermissionRationale(activity, permission)
    }

    @JvmStatic
    fun requestPermissionBlocking(permission: String?, timeoutMs: Int): Boolean {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) {
            return true
        }

        if (permission.isNullOrEmpty()) {
            return false
        }

        val activity = activeActivity ?: return false
        if (ContextCompat.checkSelfPermission(activity, permission) == PackageManager.PERMISSION_GRANTED) {
            return true
        }

        val request: PendingPermissionRequest
        synchronized(permissionLock) {
            if (pendingPermissionRequest != null) {
                return false
            }

            request = PendingPermissionRequest(permission, nextPermissionRequestCode.getAndIncrement())
            pendingPermissionRequest = request
        }

        activity.runOnUiThread {
            ActivityCompat.requestPermissions(activity, arrayOf(permission), request.requestCode)
        }

        try {
            if (!request.latch.await(timeoutMs.toLong(), TimeUnit.MILLISECONDS)) {
                synchronized(permissionLock) {
                    if (pendingPermissionRequest === request) {
                        pendingPermissionRequest = null
                    }
                }

                return ContextCompat.checkSelfPermission(activity, permission) == PackageManager.PERMISSION_GRANTED
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()

            synchronized(permissionLock) {
                if (pendingPermissionRequest === request) {
                    pendingPermissionRequest = null
                }
            }

            return false
        }

        return request.granted || ContextCompat.checkSelfPermission(activity, permission) == PackageManager.PERMISSION_GRANTED
    }

    @JvmStatic
    fun onRequestPermissionsResult(requestCode: Int, permissions: Array<String>?, grantResults: IntArray?) {
        val request: PendingPermissionRequest
        synchronized(permissionLock) {
            val pending = pendingPermissionRequest
            if (pending == null || pending.requestCode != requestCode) {
                return
            }

            request = pending
            pendingPermissionRequest = null
        }

        val permissionMatches = permissions != null
            && permissions.isNotEmpty()
            && request.permission == permissions[0]

        request.granted = permissionMatches
            && grantResults != null
            && grantResults.isNotEmpty()
            && grantResults[0] == PackageManager.PERMISSION_GRANTED
        request.latch.countDown()
    }

    private fun resolvePermissionContext(): Context? {
        return activeActivity ?: MainService.getActiveContext()
    }
}
