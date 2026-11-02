# SDL and our bridge are called from native code via JNI: keep names.
-keep class org.libsdl.app.** { *; }
-keep class com.gl328.multigamehub.** { *; }
-dontwarn org.libsdl.app.**
