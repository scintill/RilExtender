LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)
LOCAL_SRC_FILES:= ../elf.c ../inject.c ../ptrace.c
LOCAL_MODULE := inject
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS := -DANDROID -DTHUMB
include $(BUILD_EXECUTABLE)
