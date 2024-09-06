LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE    := ota_coordinator
LOCAL_SRC_FILES := ota_coordinator.c device_helper.c
LOCAL_SHARED_LIBRARIES := liblog
include $(BUILD_EXECUTABLE)
