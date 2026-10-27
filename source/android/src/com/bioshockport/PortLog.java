package com.bioshockport;
/** Production port has no periodic diagnostic/logcat output. */
final class PortLog {
    static final boolean ENABLED=false;
    static int i(String tag,String text){return 0;}
    static int e(String tag,String text){return 0;}
    static int e(String tag,String text,Throwable error){return 0;}
    static int w(String tag,String text){return 0;}
}
