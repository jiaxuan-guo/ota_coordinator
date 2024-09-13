LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_CFLAGS := -Wall -Werror
LOCAL_LICENSE_KINDS:= SPDX-license-identifier-Apache-2.0

LOCAL_MODULE    := ota_coordinator
LOCAL_SRC_FILES := ota_coordinator.c device_helper.c
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_EXECUTABLE)
