/*
 *
 * Dumb userspace USB Audio receiver
 * Copyright 2012 Joel Stanley <joel@jms.id.au>
 *
 * Based on the following:
 *
 * libusb example program to measure Atmel SAM3U isochronous performance
 * Copyright (C) 2012 Harald Welte <laforge@gnumonks.org>
 *
 * Copied with the author's permission under LGPL-2.1 from
 * http://git.gnumonks.org/cgi-bin/gitweb.cgi?p=sam3u-tests.git;a=blob;f=usb-benchmark-project/host/benchmark.c;h=74959f7ee88f1597286cd435f312a8ff52c56b7e
 *
 * An Atmel SAM3U test firmware is also available in the above repository.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <pthread.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <libusb.h>

#include <jni.h>

#include <android/log.h>
#define LOG_TAG "UsbAudioNative"
#define LOGD(...) \
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define UNUSED __attribute__((unused))

#define DEFAULT_USBFS "/dev/bus/usb"
#define NUM_TRANSFERS 10
#define PACKET_SIZE 192
#define NUM_PACKETS 10

/* Structure to contain all our information, so we can pass it to callbacks */
typedef struct _CustomData {
    struct libusb_device_handle *dev_handle;
    libusb_context *usb_ctx;
    int interface_idx;
    int interface_ep;
    struct libusb_transfer *xfr[NUM_TRANSFERS];
    uint8_t *xfr_buf[NUM_TRANSFERS];
    unsigned long num_bytes;
    unsigned long num_xfer;
    struct timeval tv_start;
    int do_exit;
    pthread_mutex_t cb_mutex;
    pthread_cond_t cb_cond;
} CustomData;

static JavaVM* java_vm = NULL;

static jclass au_id_jms_usbaudio_AudioPlayback = NULL;
static jmethodID au_id_jms_usbaudio_AudioPlayback_write;

static CustomData *customData;

static void delete_transfer(CustomData *p_data, struct libusb_transfer *transfer) {
    int i, ret;

    pthread_mutex_lock(&p_data->cb_mutex);

    for (i=0; i<NUM_TRANSFERS; i++) {
        if (p_data->xfr[i] == transfer) {
            ret = libusb_cancel_transfer(p_data->xfr[i]);
            //LOGD("delete_transfer: %d %s", i, libusb_error_name(ret));
            free(transfer->buffer);
            p_data->xfr_buf[i] = NULL;
            libusb_free_transfer(transfer);
            p_data->xfr[i] = NULL;
            break;
        }
    }

    pthread_cond_broadcast(&p_data->cb_cond);

    pthread_mutex_unlock(&p_data->cb_mutex);
}

static void cb_xfr(struct libusb_transfer *xfr)
{
	unsigned int i;

    int len = 0;
    int resubmit = 1;

    CustomData *p_data = xfr->user_data;
    if (!p_data) return; // cancel callback

    // Get an env handle
    JNIEnv * env;
    void * void_env;
    bool had_to_attach = false;
    jint status = (*java_vm)->GetEnv(java_vm, &void_env, JNI_VERSION_1_6);

    if (status == JNI_EDETACHED) {
        (*java_vm)->AttachCurrentThread(java_vm, &env, NULL);
        had_to_attach = true;
    } else {
        env = void_env;
    }

    // Create a jbyteArray.
    int start = 0;
    jbyteArray audioByteArray = (*env)->NewByteArray(env, PACKET_SIZE * xfr->num_iso_packets);
    for (i = 0; i < xfr->num_iso_packets; i++) {
        struct libusb_iso_packet_descriptor *pack = &xfr->iso_packet_desc[i];

        if (pack->status != LIBUSB_TRANSFER_COMPLETED) {
            LOGE("Error (status %d: %s) :", pack->status,
                    libusb_error_name(pack->status));
            /* This doesn't happen, so bail out if it does. */
            (*env)->DeleteLocalRef(env, audioByteArray);
            resubmit = 0;
            goto detach;// exit(EXIT_FAILURE);
        }

        const uint8_t *data = libusb_get_iso_packet_buffer_simple(xfr, i);
        (*env)->SetByteArrayRegion(env, audioByteArray, len, pack->length, data);

        len += pack->length;
    }

    // Call write()
    (*env)->CallStaticVoidMethod(env, au_id_jms_usbaudio_AudioPlayback,
            au_id_jms_usbaudio_AudioPlayback_write, audioByteArray);
    (*env)->DeleteLocalRef(env, audioByteArray);
    if ((*env)->ExceptionCheck(env)) {
        LOGE("Exception while trying to pass sound data to java");
        goto detach;
    }

    p_data->num_bytes += len;
    p_data->num_xfer++;
detach:
    if (had_to_attach) {
        (*java_vm)->DetachCurrentThread(java_vm);
    }
    if (resubmit && !p_data->do_exit) {
        if (libusb_submit_transfer(xfr) < 0) {
            LOGE("error re-submitting URB");
            //exit(1);
            delete_transfer(p_data, xfr);
        }
    } else {
        //LOGD("don't re-submitting URB");
        delete_transfer(p_data, xfr);
    }
}

static int benchmark_in(CustomData *p_data, uint8_t ep)
{
	//static uint8_t buf[PACKET_SIZE * NUM_PACKETS];
	//static struct libusb_transfer *xfr[NUM_TRANSFERS];
    size_t total_transfer_size = PACKET_SIZE * NUM_PACKETS;
	int num_iso_pack = NUM_PACKETS;
    int i, ret;

    if (!p_data || !p_data->dev_handle) return 0;
	/* NOTE: To reach maximum possible performance the program must
	 * submit *multiple* transfers here, not just one.
	 *
	 * When only one transfer is submitted there is a gap in the bus
	 * schedule from when the transfer completes until a new transfer
	 * is submitted by the callback. This causes some jitter for
	 * isochronous transfers and loss of throughput for bulk transfers.
	 *
	 * This is avoided by queueing multiple transfers in advance, so
	 * that the host controller is always kept busy, and will schedule
	 * more transfers on the bus while the callback is running for
	 * transfers which have completed on the bus.
	 */
    for (i=0; i<NUM_TRANSFERS; i++) {
        p_data->xfr[i] = libusb_alloc_transfer(num_iso_pack);
        p_data->xfr_buf[i] = malloc(total_transfer_size);
        if (!p_data->xfr[i]) {
            LOGE("Could not allocate transfer %d", i);
            return -ENOMEM;
        }

        libusb_fill_iso_transfer(p_data->xfr[i], p_data->dev_handle, ep,
                p_data->xfr_buf[i], total_transfer_size, num_iso_pack, cb_xfr, (void*)p_data, 1000);
        libusb_set_iso_packet_lengths(p_data->xfr[i], total_transfer_size/num_iso_pack);
        ret = libusb_submit_transfer(p_data->xfr[i]);
        LOGD("libusb_submit_transfer: %d %s", i, libusb_error_name(ret));
    }

	gettimeofday(&p_data->tv_start, NULL);

    return 1;
}

static int cancel_transfer(CustomData *p_data) {
    int i, ret;
    if (!p_data || !p_data->dev_handle) return 0;

    pthread_mutex_lock(&p_data->cb_mutex);

    for (i=0; i<NUM_TRANSFERS; i++) {
        if (p_data->xfr[i]) {
            ret = libusb_cancel_transfer(p_data->xfr[i]);
// this could lead to crash in cb_xfr, see libuvc/stream.c/uvc_stream_stop
//            free(p_data->xfr_buf[i]);
//            libusb_free_transfer(p_data->xfr[i]);
//            p_data->xfr[i] = NULL;
        }
    }
    /* Wait for transfers to complete/cancel */
    for (ret = 0; ret < 10; ret++) {
        for (i = 0; i < NUM_TRANSFERS; i++) {
            if (p_data->xfr[i] != NULL) {
                break;
            }
        }
        if (i == NUM_TRANSFERS)
            break;
        //sleep(5);
        pthread_cond_wait(&p_data->cb_cond, &p_data->cb_mutex);
    }

    pthread_cond_broadcast(&p_data->cb_cond);
    pthread_mutex_unlock(&p_data->cb_mutex);

    LOGD("all transfer being cancelled");
    return 1;
}

unsigned int measure(CustomData *p_data)
{
	struct timeval tv_stop;
	unsigned int diff_msec;

    if (!p_data) return 0;

	gettimeofday(&tv_stop, NULL);

	diff_msec = (tv_stop.tv_sec - p_data->tv_start.tv_sec)*1000;
	diff_msec += (tv_stop.tv_usec - p_data->tv_start.tv_usec)/1000;

	LOGD("%lu transfers (total %lu bytes) in %u milliseconds => %lu bytes/sec",
         p_data->num_xfer, p_data->num_bytes, diff_msec,
         (p_data->num_bytes*1000)/diff_msec);

    return p_data->num_bytes;
}

JNIEXPORT jint JNICALL
Java_au_id_jms_usbaudio_UsbAudio_measure(JNIEnv* env UNUSED, jobject foo UNUSED) {
    return measure(customData);
}

JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved UNUSED)
{
    LOGD("libusbaudio: loaded");
    java_vm = vm;
    customData = (CustomData*)calloc(sizeof(CustomData), 1);

    JNIEnv * env;
    (*java_vm)->GetEnv(vm, &env, JNI_VERSION_1_6);

    // Get write callback handle
    jclass clazz = (*env)->FindClass(env, "au/id/jms/usbaudio/AudioPlayback");
    if (!clazz) {
        LOGE("Could not find au.id.jms.usbaudio.AudioPlayback");
        return JNI_ERR;
    }
    au_id_jms_usbaudio_AudioPlayback = (*env)->NewGlobalRef(env, clazz);

    au_id_jms_usbaudio_AudioPlayback_write = (*env)->GetStaticMethodID(env,
           au_id_jms_usbaudio_AudioPlayback, "write", "([B)V");
    if (!au_id_jms_usbaudio_AudioPlayback_write) {
        LOGE("Could not find au.id.jms.usbaudio.AudioPlayback");
        (*env)->DeleteGlobalRef(env, au_id_jms_usbaudio_AudioPlayback);
        return JNI_ERR;
    }

    return JNI_VERSION_1_6;
}


JNIEXPORT void JNI_OnUnload(JavaVM* vm, void* reserved UNUSED)
{
    JNIEnv * env;
    void * void_env;
    (*java_vm)->GetEnv(vm, &void_env, JNI_VERSION_1_6);
    env = void_env;

    (*env)->DeleteGlobalRef(env, au_id_jms_usbaudio_AudioPlayback);
    free(customData);
    LOGD("libusbaudio: unloaded");
}

JNIEXPORT jboolean JNICALL
Java_au_id_jms_usbaudio_UsbAudio_setup(JNIEnv* env UNUSED, jobject foo UNUSED,
                                       jint vid, jint pid, jint fd, jint bus, jint dev) {
    if (!customData) return false;
    struct libusb_device *usb_dev;
    struct libusb_config_descriptor *config;
    const struct libusb_interface_descriptor *if_desc;
    int rc, idx, alt, endp;

    if (customData->usb_ctx == NULL) {
        rc = libusb_init2(&customData->usb_ctx, DEFAULT_USBFS);
    } else {//already initialized
        rc = 0;
    }
	if (rc < 0) {
		LOGE("Error initializing libusb: %s", libusb_error_name(rc));
        return JNI_FALSE;
	}

	LOGD("open usb device 0x%04x:0x%04x on %s/%03d/%03d with fd = %d",
        vid, pid, DEFAULT_USBFS, bus, dev, fd);
    usb_dev = libusb_get_device_with_fd(customData->usb_ctx, vid, pid, NULL, fd, bus, dev);
	libusb_set_device_fd(usb_dev, fd); // assign fd to libusb_device for non-rooted Android devices
	rc = libusb_open(usb_dev, &customData->dev_handle);
	if (!customData->dev_handle) {
        LOGE("Error finding USB device /dev/bus/usb/%03d/%03d", bus, dev);
        goto failed;
	}

	// find usb config
    rc = libusb_get_config_descriptor(usb_dev, 0, &config);
    LOGD("get config: %s", libusb_error_name(rc));
    if (!rc) {
        for (idx = 0; idx < config->bNumInterfaces; ++idx) {
            if_desc = &config->interface[idx].altsetting[0];
            if (if_desc->bInterfaceClass == 1 && if_desc->bInterfaceSubClass == 2) {
                for (alt = 0; alt < config->interface[idx].num_altsetting; alt++) {
                    if_desc = &config->interface[idx].altsetting[alt];
                    if (if_desc->bInterfaceClass == 1 && if_desc->bInterfaceSubClass == 2 &&
                        if_desc->bNumEndpoints > 0) { // found UAC AudioStreaming endpoint
                        goto found;
                    }
                    if_desc = NULL;
                }
            }
        }
    }
found:
    if (if_desc) {
        customData->interface_idx = idx;
        endp = if_desc->endpoint[0].bEndpointAddress;
        customData->interface_ep = endp;
        LOGD("found AudioStreaming(%d-%d) bEndpointAddress: %d", idx, alt, endp);
    }

    // enable kernel driver
    rc = libusb_kernel_driver_active(customData->dev_handle, idx);
    LOGD("active kernel driver: %s", libusb_error_name(rc));
    if (rc == 1) {
        rc = libusb_detach_kernel_driver(customData->dev_handle, idx);
        if (rc < 0) {
            LOGE("Could not detach kernel driver: %s", libusb_error_name(rc));
            goto failed;
        }
    }

	rc = libusb_claim_interface(customData->dev_handle, idx);
	if (rc < 0) {
        LOGE("Error claiming interface: %s", libusb_error_name(rc));
        goto failed;
    }

	rc = libusb_set_interface_alt_setting(customData->dev_handle, idx, alt);
	if (rc < 0) {
        LOGE("Error setting alt setting: %s", libusb_error_name(rc));
        goto failed;
	}

	// BU113: set frequency 48000, suppose to be worked on other devices
    uint8_t rate[3];
    rate[0] = 0x80;
    rate[1] = 0xBB;
    rate[2] = 0x00;
	rc = libusb_control_transfer(customData->dev_handle,
            LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_ENDPOINT,
            0x01, 0x0100, endp, rate, sizeof(rate), 0);
    if (rc == sizeof(rate)) {
        LOGD("set mic config success:0x%02x:0x%02x:0x%02x\n", rate[0], rate[1], rate[2]);
    }

    pthread_mutex_init(&customData->cb_mutex, NULL);
    pthread_cond_init(&customData->cb_cond, NULL);

    return JNI_TRUE;

failed:
    if (customData->dev_handle)
        libusb_close(customData->dev_handle);
    customData->dev_handle = NULL;
    libusb_exit(customData->usb_ctx);
    customData->usb_ctx = NULL;
    return JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_au_id_jms_usbaudio_UsbAudio_start(JNIEnv* env UNUSED, jobject foo UNUSED) {
    int rc;
    if (!customData) return JNI_FALSE;
    // Good to go
    customData->do_exit = 0;
    LOGD("Starting capture");
    if ((rc = benchmark_in(customData, customData->interface_ep)) < 0) {
        LOGE("Capture failed to start: %d", rc);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_au_id_jms_usbaudio_UsbAudio_stop(JNIEnv* env UNUSED, jobject foo UNUSED) {
    if (!customData) return JNI_FALSE;
    if (customData->do_exit) return JNI_TRUE;//already stopped
    customData->do_exit = 1;
    measure(customData);
    cancel_transfer(customData);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_au_id_jms_usbaudio_UsbAudio_close(JNIEnv* env UNUSED, jobject foo UNUSED) {
    int rc;
    if (!customData) return JNI_FALSE;
    if (customData->do_exit == 0) {
        return JNI_FALSE;
    }
    if (!customData->usb_ctx) return JNI_TRUE;//already closed
    rc = libusb_release_interface(customData->dev_handle, customData->interface_idx);
    LOGD("libusb_release_interface %d: %s", customData->interface_idx, libusb_error_name(rc));
	if (customData->dev_handle) {
        libusb_close(customData->dev_handle);
	}
    customData->dev_handle = NULL;
	libusb_exit(customData->usb_ctx);
    customData->usb_ctx = NULL;

    pthread_cond_destroy(&customData->cb_cond);
    pthread_mutex_destroy(&customData->cb_mutex);

    return JNI_TRUE;
}


JNIEXPORT void JNICALL
Java_au_id_jms_usbaudio_UsbAudio_loop(JNIEnv* env UNUSED, jobject foo UNUSED) {
    if (!customData) return;
    int rc;
	while (!customData->do_exit) {
		rc = libusb_handle_events(customData->usb_ctx);
		if (rc != LIBUSB_SUCCESS) {
            LOGE("Error libusb_handle_events: %s", libusb_error_name(rc));
            break;
		}
	}
}
