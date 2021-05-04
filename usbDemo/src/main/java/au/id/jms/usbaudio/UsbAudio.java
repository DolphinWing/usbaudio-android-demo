package au.id.jms.usbaudio;

import com.serenegiant.usb.USBMonitor;

/**
 * Reference from https://github.com/shenki/usbaudio-android-demo
 * <p>
 * USB Audio native
 */
public class UsbAudio {
    static {
        System.loadLibrary("usbaudio");
    }

    /**
     * Setup USB audio device.
     *
     * @param vid vendor id
     * @param pid product id
     * @param fd  file descriptor
     * @param bus bus
     * @param dev device
     * @return true if setup successfully.
     */
    public native boolean setup(int vid, int pid, int fd, int bus, int dev);

    /**
     * Close USB audio device.
     *
     * @return true if device closed.
     */
    public native boolean close();

    /**
     * Loop usb event dispatcher.
     */
    public native void loop();

    /**
     * Start playing.
     *
     * @return true if play procedure success
     */
    public native boolean start();

    /**
     * Stop playing.
     *
     * @return true if stop procedure success
     */
    public native boolean stop();

    /**
     * Get this session statistics.
     *
     * @return
     */
    public native int measure();

    /**
     * Open USB audio device.
     *
     * @param ctrlBlock USB control block
     */
    public void open(final USBMonitor.UsbControlBlock ctrlBlock) {
        if (ctrlBlock == null) {
            throw new NullPointerException("no UsbControlBlock");
        }

        setup(ctrlBlock.getVenderId(), ctrlBlock.getProductId(), ctrlBlock.getFileDescriptor(),
                ctrlBlock.getBusNum(), ctrlBlock.getDevNum());
    }
}
