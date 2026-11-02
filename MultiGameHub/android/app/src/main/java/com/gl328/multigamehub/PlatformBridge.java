package com.gl328.multigamehub;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.Uri;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.provider.Settings;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Android services called from C++ over JNI (see src/platform/android/AndroidPlatform.cpp).
 * Only platform plumbing lives here: HTTPS downloads, the system package installer,
 * signature checks and vibration. No downloaded code is ever loaded or executed.
 */
public final class PlatformBridge {
    private PlatformBridge() {}

    // ------------------------------------------------------------------ downloads

    private static final int ST_RUNNING = 1, ST_DONE = 2, ST_FAILED = 3, ST_CANCELLED = 4;

    private static final class Download {
        volatile int state = ST_RUNNING;
        volatile long received = 0;
        volatile long total = -1;
        volatile boolean cancel = false;
        volatile String error = "";
    }

    private static final Map<Integer, Download> downloads = new HashMap<>();
    private static final AtomicInteger nextId = new AtomicInteger(1);

    public static int startDownload(final String url, final String dest, final long maxBytes) {
        if (url == null || !url.startsWith("https://")) return 0;
        final Download d = new Download();
        final int id = nextId.getAndIncrement();
        synchronized (downloads) {
            downloads.put(id, d);
        }
        Thread t = new Thread(new Runnable() {
            @Override
            public void run() {
                runDownload(d, url, dest, maxBytes);
            }
        }, "mgh-download-" + id);
        t.setDaemon(true);
        t.start();
        return id;
    }

    private static void runDownload(Download d, String url, String dest, long maxBytes) {
        HttpURLConnection conn = null;
        File out = new File(dest);
        try {
            URL current = new URL(url);
            // Follow redirects manually so every hop is verified to be HTTPS.
            for (int hop = 0; hop < 6; hop++) {
                if (!"https".equalsIgnoreCase(current.getProtocol())) throw new Exception("insecure redirect refused");
                conn = (HttpURLConnection) current.openConnection();
                conn.setInstanceFollowRedirects(false);
                conn.setConnectTimeout(15000);
                conn.setReadTimeout(30000);
                conn.setRequestProperty("User-Agent", "GotBored-Android");
                conn.setRequestProperty("Accept", "application/octet-stream, */*");
                int code = conn.getResponseCode();
                if (code >= 300 && code < 400) {
                    String loc = conn.getHeaderField("Location");
                    conn.disconnect();
                    conn = null;
                    if (loc == null) throw new Exception("redirect without location");
                    current = new URL(current, loc);
                    continue;
                }
                if (code != 200) throw new Exception("HTTP " + code);
                break;
            }
            if (conn == null) throw new Exception("too many redirects");
            long len = conn.getContentLengthLong();
            if (len > maxBytes) throw new Exception("file larger than expected");
            d.total = len;
            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            try (InputStream in = conn.getInputStream(); OutputStream os = new FileOutputStream(out)) {
                byte[] buf = new byte[32768];
                int n;
                while ((n = in.read(buf)) > 0) {
                    if (d.cancel) {
                        d.state = ST_CANCELLED;
                        return;
                    }
                    d.received += n;
                    if (d.received > maxBytes) throw new Exception("download exceeded expected size");
                    os.write(buf, 0, n);
                }
            }
            d.state = ST_DONE;
        } catch (Exception e) {
            d.error = e.getMessage() == null ? e.getClass().getSimpleName() : e.getMessage();
            d.state = d.cancel ? ST_CANCELLED : ST_FAILED;
        } finally {
            if (conn != null) conn.disconnect();
            if (d.state != ST_DONE) out.delete();
        }
    }

    private static Download get(int id) {
        synchronized (downloads) {
            return downloads.get(id);
        }
    }

    public static long[] pollDownload(int id) {
        Download d = get(id);
        if (d == null) return new long[] {0, 0, -1};
        return new long[] {d.state, d.received, d.total};
    }

    public static String downloadError(int id) {
        Download d = get(id);
        return d == null ? "" : d.error;
    }

    public static void cancelDownload(int id) {
        Download d = get(id);
        if (d != null) d.cancel = true;
    }

    public static void releaseDownload(int id) {
        synchronized (downloads) {
            Download d = downloads.remove(id);
            if (d != null) d.cancel = true;
        }
    }

    // ------------------------------------------------------------------ installer

    // Updates are handed to Android's own installer screen (ACTION_VIEW on a
    // content:// URI). This is the most compatible self-update path across
    // manufacturers; the PackageInstaller-session flow used before could be
    // aborted on some devices (e.g. Samsung) after the user tapped "Update".
    private static final int IN_IDLE = 0, IN_PENDING = 1, IN_WAITING = 2, IN_SUCCESS = 3, IN_FAILED = 4;
    private static volatile int installState = IN_IDLE;
    private static volatile String installMessage = "";
    private static volatile boolean installerShown = false;

    static void init(Context ctx) {}

    static void shutdown(Context ctx) {}

    /** Called from HubActivity.onPause: the system installer screen has covered the app. */
    static void onActivityPaused() {
        if (installState == IN_WAITING) installerShown = true;
    }

    /**
     * Called from HubActivity.onResume. A successful update replaces and restarts the
     * app, so coming back to this same process means the update was not installed.
     */
    static void onActivityResumed() {
        if (installState == IN_WAITING && installerShown) {
            installMessage = "The update wasn't installed. Tap Install update to try again.";
            installState = IN_FAILED;
            installerShown = false;
        }
    }

    public static boolean canRequestInstall(Context ctx) {
        if (Build.VERSION.SDK_INT < 26) return true;  // governed by the global "unknown sources" setting
        return ctx.getPackageManager().canRequestPackageInstalls();
    }

    public static void openInstallSettings(Context ctx) {
        if (Build.VERSION.SDK_INT < 26) return;
        Intent i = new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                Uri.parse("package:" + ctx.getPackageName()));
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        ctx.startActivity(i);
    }

    @SuppressWarnings("deprecation")
    private static Signature[] signers(PackageInfo info) {
        if (info == null) return null;
        if (Build.VERSION.SDK_INT >= 28) {
            if (info.signingInfo == null) return null;
            return info.signingInfo.hasMultipleSigners()
                    ? info.signingInfo.getApkContentsSigners()
                    : info.signingInfo.getSigningCertificateHistory();
        }
        return info.signatures;
    }

    /** Returns null when the APK is a valid update for this app, otherwise a reason. */
    @SuppressWarnings("deprecation")
    public static String verifyApk(Context ctx, String path) {
        try {
            PackageManager pm = ctx.getPackageManager();
            int flags = Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES : PackageManager.GET_SIGNATURES;
            PackageInfo apk = pm.getPackageArchiveInfo(path, flags);
            if (apk == null) return "file is not a valid APK";
            if (apk.applicationInfo != null) {
                apk.applicationInfo.sourceDir = path;
                apk.applicationInfo.publicSourceDir = path;
            }
            if (!ctx.getPackageName().equals(apk.packageName)) return "package name does not match";
            PackageInfo self = pm.getPackageInfo(ctx.getPackageName(), flags);
            long selfCode = Build.VERSION.SDK_INT >= 28 ? self.getLongVersionCode() : self.versionCode;
            long apkCode = Build.VERSION.SDK_INT >= 28 ? apk.getLongVersionCode() : apk.versionCode;
            if (apkCode <= selfCode) return "APK is not newer than the installed version";
            Signature[] a = signers(apk), b = signers(self);
            if (a == null || b == null || a.length == 0) return "APK signature could not be read";
            // The current signing certificate of the installed app must appear in the APK's signers.
            boolean match = false;
            for (Signature s : a) {
                if (Arrays.equals(s.toByteArray(), b[b.length - 1].toByteArray())) match = true;
            }
            if (!match) return "APK is signed with a different key";
            return null;
        } catch (Exception e) {
            return "verification error: " + e.getMessage();
        }
    }

    /** Opens Android's installer for the verified APK. Returns null on success, otherwise an error. */
    public static String installApk(Context ctx, String path) {
        try {
            if (!new File(path).isFile()) return "update file is missing, please download again";
            Uri uri = ApkProvider.register(path);
            Intent i = new Intent(Intent.ACTION_VIEW);
            i.setDataAndType(uri, ApkProvider.APK_MIME);
            i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
            if (!(ctx instanceof Activity)) i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            installMessage = "";
            installerShown = false;
            installState = IN_WAITING;
            ctx.startActivity(i);
            return null;
        } catch (Exception e) {
            installState = IN_FAILED;
            return "could not open the installer: " + e.getMessage();
        }
    }

    public static int installState() {
        return installState;
    }

    public static String installMessage() {
        return installMessage;
    }

    // ------------------------------------------------------------------ misc

    @SuppressWarnings("deprecation")
    public static void vibrate(Context ctx, int ms) {
        try {
            Vibrator v = (Vibrator) ctx.getSystemService(Context.VIBRATOR_SERVICE);
            if (v == null || !v.hasVibrator()) return;
            if (Build.VERSION.SDK_INT >= 26) {
                v.vibrate(VibrationEffect.createOneShot(ms, VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                v.vibrate(ms);
            }
        } catch (Exception ignored) {
        }
    }

    public static long apkSize(Context ctx) {
        long total = new File(ctx.getApplicationInfo().sourceDir).length();
        String[] splits = ctx.getApplicationInfo().splitSourceDirs;
        if (splits != null) for (String s : splits) total += new File(s).length();
        return total;
    }

    public static void moveToBack(final Activity activity) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.moveTaskToBack(true);
            }
        });
    }
}
