package com.gl328.multigamehub;

import android.os.Bundle;

import org.libsdl.app.SDLActivity;

/**
 * Thin launcher activity. All application logic lives in C++ (libmain.so);
 * SDL3 is linked statically into that library, so only "main" is loaded.
 */
public class HubActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {"main"};
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        PlatformBridge.init(getApplicationContext());
    }

    @Override
    protected void onDestroy() {
        PlatformBridge.shutdown(getApplicationContext());
        super.onDestroy();
    }
}
