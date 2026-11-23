package com.moonydev.ledgevrmobile;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.View;
import android.Manifest;
import android.content.pm.PackageManager;
import android.widget.Toast;

/**
 * LEDGE Mobile — thin Java layer for the touch (non-VR) build.
 * The whole game lives in libledegemobile.so (native EGL + GLES).
 * Same bridges as the VR build: WsBridge (Photon) and HttpBridge (PlayFab).
 */
public class MainActivity extends NativeActivity {
    static {
        // The mobile shell — no OpenXR loader needed.
        System.loadLibrary("ledgemobile");
    }

    private static final int MIC_REQUEST_CODE = 0x1ED6E;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        hideSystemUi();

        // Voice chat needs the mic. Request early so the user only sees the
        // dialog once; the game works without it (voice just stays muted).
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},
                    MIC_REQUEST_CODE);
        }
    }

    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == MIC_REQUEST_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "LEDGE Mobile: microphone ready for voice chat",
                        Toast.LENGTH_SHORT).show();
            }
        }
    }
}
