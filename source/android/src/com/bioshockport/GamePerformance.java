package com.bioshockport;



/** Tell OpenXR which Wine threads actually execute and render the game. */
final class GamePerformance implements AutoCloseable {
    private static native int tune(boolean force);
    private volatile boolean closed;
    private volatile boolean force=true;
    private final Thread worker;

    GamePerformance() {
        // NativeCinema has already loaded the shared library. Work happens
        // outside the XR render loop; a missing optional API keeps the game running.
        worker=new Thread(() -> {
            while(!closed) {
                boolean refresh=force;force=false;
                try {tune(refresh);}
                catch(UnsatisfiedLinkError error){PortLog.e("BioshockPerf","Game thread priorities unavailable",error);break;}
                try {Thread.sleep(2000);}catch(InterruptedException ignored){}
            }
        },"BioShock-CPU");
        worker.setDaemon(true);
        worker.start();
    }

    void wake(){force=true;worker.interrupt();}

    @Override public void close() {
        closed=true;
        worker.interrupt();
    }
}
