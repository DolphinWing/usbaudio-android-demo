package au.id.jms.usbaudio;

import android.content.Context;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbManager;

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
     * @param context Android Context
     * @param device  USB device
     */
    public boolean open(final Context context, final UsbDevice device) {
        if (context == null) {
            throw new NullPointerException("no Android Context");
        }
        if (device == null) {
            throw new NullPointerException("no USB device");
        }

        UsbManager manager = (UsbManager) context.getSystemService(Context.USB_SERVICE);
        if (manager != null) {
            int fd = 0;
            try {
                UsbDeviceConnection conn = manager.openDevice(device);
                fd = conn.getFileDescriptor();
            } catch (Exception e) {
                e.printStackTrace();
            }
            String[] name = device.getDeviceName().split("/");
            int busnum = 0;
            int devnum = 0;
            if (name != null && name.length > 2) {
                busnum = Integer.parseInt(name[name.length - 2]);
                devnum = Integer.parseInt(name[name.length - 1]);
            }
            return setup(device.getVendorId(), device.getProductId(), fd, busnum, devnum);
        }
        return false;
    }
}
