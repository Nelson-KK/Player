package com.nelson.player.player;

public class PlayerNative {
    static {
        System.loadLibrary("native-lib");
    }

    public static native String getVersion();

    public static native void init(FFmpegCallBack callBack);

    public static native void setDataSource();

    public static native void start();

    public static native void prepare();
}