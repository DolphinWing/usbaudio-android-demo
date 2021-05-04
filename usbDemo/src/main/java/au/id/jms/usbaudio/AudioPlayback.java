package au.id.jms.usbaudio;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.util.Log;

/**
 * Reference from https://github.com/shenki/usbaudio-android-demo
 * <p>
 * A playback callback from native code to play UAC.
 */
public class AudioPlayback {
    private static final String TAG = "AudioPlayback";

    private static final int SAMPLE_RATE_HZ = 48000;

    /**
     * Callback interface that UAC has receive data or other things.
     */
    public interface Callback {
        /**
         * called when a new audio data coming
         *
         * @param data decoded audio data
         */
        void onData(byte[] data);
    }

    private static AudioTrack track = null;
    private static Callback callback = null;

    /**
     * Setup audio track playback
     *
     * @param cb optional callback
     */
    public static void setup(Callback cb) {
        Log.i(TAG, "Audio Playback");

        int bufSize = AudioTrack.getMinBufferSize(SAMPLE_RATE_HZ,
                AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        Log.d(TAG, "Buffer size: " + bufSize);

        release(); // make sure the old track is released
        track = new AudioTrack(AudioManager.STREAM_MUSIC,
                SAMPLE_RATE_HZ,
                AudioFormat.CHANNEL_OUT_STEREO,
                AudioFormat.ENCODING_PCM_16BIT,
                bufSize,
                AudioTrack.MODE_STREAM);
        // track.play();
        callback = cb;
    }

    /**
     * Start audio playback
     */
    public static void start() {
        // Log.d(TAG, "start AudioTrack " + track);
        if (track != null) track.play();
    }

    /**
     * Stop audio playback
     */
    public static void stop() {
        if (track != null) track.stop();
        // Log.d(TAG, "stop AudioTrack " + track);
    }

    /**
     * Release audio track
     */
    public static void release() {
        callback = null;
        if (track != null) {
            // track.stop();
            track.release();
            track = null;
        }
    }

    /**
     * A callback entry for native code.
     *
     * @param decodedAudio decoded audio data
     */
    public static void write(byte[] decodedAudio) {
        // Log.d(TAG, String.format("size = %d", decodedAudio.length));
        try {
            if (track != null) track.write(decodedAudio, 0, decodedAudio.length);
            if (callback != null) callback.onData(decodedAudio);
        } catch (Exception e) {
            Log.e(TAG, "write: " + e.getMessage());
            e.printStackTrace();
        }
    }
}
