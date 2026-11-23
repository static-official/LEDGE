package com.moonydev.ledgevr;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.View;
import android.Manifest;
import android.content.pm.PackageManager;
import android.widget.Toast;

/**
 * LEDGE VR — thin Java layer.
 * The whole game lives in libv40shell.so (native OpenXR).
 * This class only handles Android-level concerns: runtime permissions
 * (microphone for voice chat), immersive mode and crash reporting hooks.
 */
public class MainActivity extends NativeActivity {
    static {
        // libopenxr_loader.so is loaded first: it registers the OpenXR loader.
        System.loadLibrary("openxr_loader");
        // The game shell.
        System.loadLibrary("v40shell");
    }

    private static final int MIC_REQUEST_CODE = 0x1ED6E;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Hide the OS navigation; the VR shell owns the whole screen.
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                        | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                        | View.SYSTEM_UI_FLAG_FULLSCREEN
                        | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        // Voice chat needs the mic. Request early so the user only sees the
        // dialog once; the game works without it (voice just stays muted).
        if (checkSelfPermission(Manifest.permission.RECORD_AUDIO)
                != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.RECORD_AUDIO},
                    MIC_REQUEST_CODE);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                            | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                            | View.SYSTEM_UI_FLAG_FULLSCREEN
                            | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == MIC_REQUEST_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this, "LEDGE VR: microphone ready for voice chat",
                        Toast.LENGTH_SHORT).show();
            }
        }
    }
}
