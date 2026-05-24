package org.qtproject.qt.android.bindings;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

public class FrontendQuitReceiver extends BroadcastReceiver {
    private static final String TAG = "main";

    @Override
    public void onReceive(Context context, Intent intent) {
        Log.d(TAG, "Frontend quit requested from service notification");
        QtActivity.quitFromServiceNotification();
    }
}
