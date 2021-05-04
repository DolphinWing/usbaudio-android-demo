package au.id.jms.usbaudiodemo;


import java.util.HashMap;
import java.util.Iterator;

import org.libusb.UsbHelper;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.os.Build;
import android.os.Bundle;
import android.support.v4.app.FragmentActivity;
import android.util.Log;
import android.view.Menu;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import au.id.jms.usbaudio.AudioPlayback;
import au.id.jms.usbaudio.UsbAudio;

public class MainActivity extends FragmentActivity {
	
	private static final String TAG = "UsbAudio";
	
    private static final String ACTION_USB_PERMISSION = "com.minelab.droidspleen.USB_PERMISSION";
    PendingIntent mPermissionIntent = null;
    UsbManager mUsbManager = null;
    UsbDevice mAudioDevice = null;
    
    UsbAudio mUsbAudio = null;

	Thread mUsbThread = null;

	private UsbReciever mUsbPermissionReciever;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);
        
        Log.d(TAG, "Hello, World!");
        
        // Grab the USB Device so we can get permission
        mUsbManager = (UsbManager) getSystemService(Context.USB_SERVICE);
        HashMap<String, UsbDevice> deviceList = mUsbManager.getDeviceList();
        Iterator<UsbDevice> deviceIterator = deviceList.values().iterator();
        while(deviceIterator.hasNext()){
            UsbDevice device = deviceIterator.next();
    		UsbInterface intf = device.getInterface(0);
    		Log.d(TAG, String.format("%s: vid=0x%04x, pid=0x%04x",
					device.getDeviceName(), device.getVendorId(), device.getProductId()));
    		if (intf.getInterfaceClass() == UsbConstants.USB_CLASS_AUDIO ||
					intf.getInterfaceClass() == UsbConstants.USB_CLASS_VIDEO) {
    			Log.d(TAG, "Audio class device: " + device);
    			mAudioDevice = device;
    		}
        }
        
        // Load native lib
        System.loadLibrary("usb-1.0");
        UsbHelper.useContext(getApplicationContext());
        
    	mUsbAudio = new UsbAudio();
    	
    	AudioPlayback.setup();
    	
    	// Buttons
		final Button startButton = (Button) findViewById(R.id.button1);
		final Button stopButton = (Button) findViewById(R.id.button2);
		
		startButton.setEnabled(true);
		stopButton.setEnabled(false);
		
		startButton.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				Log.d(TAG, "Start pressed");
				int fd = 0;
				try {
					UsbDeviceConnection conn = mUsbManager.openDevice(mAudioDevice);
					fd = conn.getFileDescriptor();
					Log.d(TAG, "open device: " + fd);
				} catch (Exception e) {
					Log.e(TAG, "open device: " + e.getMessage());
					e.printStackTrace();
				}
		    	if (mUsbAudio.setup(fd) == true) {
		    		startButton.setEnabled(false);
		    		stopButton.setEnabled(true);
		    	}
		    	
		        new Thread(new Runnable() {
		            public void run() {
		            	while (true) {
		            		mUsbAudio.loop();
		            	}
		            }
		        }).start();
			}
		});
		
		stopButton.setOnClickListener(new View.OnClickListener() {
			public void onClick(View v) {
				Log.d(TAG, "Stop pressed");
		    	mUsbAudio.stop();
		    	mUsbAudio.close();
		    	
	    		startButton.setEnabled(true);
	    		stopButton.setEnabled(false);
			}
		});
        
        // Register for permission
        mPermissionIntent = PendingIntent.getBroadcast(this, 0, new Intent(ACTION_USB_PERMISSION), 0);
        IntentFilter filter = new IntentFilter(ACTION_USB_PERMISSION);
        mUsbPermissionReciever = new UsbReciever();
        registerReceiver(mUsbPermissionReciever, filter);
        
        // Request permission from user
        requestPermission();
    }
    
    @Override
    protected void onDestroy() {
    	super.onDestroy();
    	unregisterReceiver(mUsbPermissionReciever);
    	if (mUsbAudio != null) {
    		mUsbAudio.stop();
    		mUsbAudio.close();
    	}
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.activity_main, menu);
        return true;
    }

    private void setDevice(UsbDevice device) {
    	if (device != null && Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
			Log.d(TAG, "setDevice: " + device.getProductName());
		}
		// Set button to enabled when permission is obtained
    	((Button) findViewById(R.id.button1)).setEnabled(device != null);
    }
    
    private class UsbReciever extends BroadcastReceiver {
		
		@Override
		public void onReceive(Context context, Intent intent) {
			String action = intent.getAction();
			UsbDevice device = (UsbDevice) intent.getParcelableExtra(UsbManager.EXTRA_DEVICE);
			if (ACTION_USB_PERMISSION.equals(action)) {
				if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
					setDevice(device);
				} else {
					Log.d(TAG, "Permission denied for device " + device);
				}
			}
		}
    }

    private void requestPermission() {
    	if (mAudioDevice != null && mUsbManager != null) {
			boolean granted = mUsbManager.hasPermission(mAudioDevice);
			Log.d(TAG, "granted: " + granted);
		}
		if (mAudioDevice != null && mPermissionIntent != null) {
			mUsbManager.requestPermission(mAudioDevice, mPermissionIntent);
		} else {
			Log.e(TAG, "Device not present? Can't request permission");
		}
	}
}
