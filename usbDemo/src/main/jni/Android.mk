PROJ_PATH := $(call my-dir)

include $(CLEAR_VARS)
include $(PROJ_PATH)/libusb/android/jni/libusb.mk

include $(CLEAR_VARS)
include $(PROJ_PATH)/usbaudio.mk

