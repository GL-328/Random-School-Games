package com.gl328.multigamehub;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Minimal read-only provider that hands the verified update APK to Android's
 * system installer (content:// URIs are required since Android 7; this avoids
 * pulling in AndroidX FileProvider). It serves exactly one file: the path
 * registered by PlatformBridge.installApk() after SHA-256 and signature checks.
 */
public final class ApkProvider extends ContentProvider {
    static final String AUTHORITY = "com.gl328.multigamehub.apkprovider";
    static final String APK_MIME = "application/vnd.android.package-archive";
    private static volatile String allowedPath;

    static Uri register(String path) {
        allowedPath = path;
        return Uri.parse("content://" + AUTHORITY + "/update.apk");
    }

    private static File file(Uri uri) throws FileNotFoundException {
        String p = allowedPath;
        if (p == null || uri == null || !"/update.apk".equals(uri.getPath())) throw new FileNotFoundException();
        File f = new File(p);
        if (!f.isFile()) throw new FileNotFoundException();
        return f;
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (mode != null && !mode.equals("r")) throw new FileNotFoundException("read-only");
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return APK_MIME;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        File f;
        try {
            f = file(uri);
        } catch (FileNotFoundException e) {
            return null;
        }
        MatrixCursor c = new MatrixCursor(new String[] {OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE});
        c.addRow(new Object[] {"GotBored-update.apk", f.length()});
        return c;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException();
    }
}
