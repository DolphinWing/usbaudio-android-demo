LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE	:= usbaudio
LOCAL_SHARED_LIBRARIES := libusb-1.0
LOCAL_LDLIBS := -llog
#LOCAL_CFLAGS :=
LOCAL_SRC_FILES := usbaudio_dump.c
include $(BUILD_SHARED_LIBRARY)
