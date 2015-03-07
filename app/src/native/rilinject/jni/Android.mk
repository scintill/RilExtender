# Copyright (c) 2014 Joey Hewitt
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

LOCAL_PATH := $(call my-dir)

# http://stackoverflow.com/a/5476363
include $(CLEAR_VARS)

LOCAL_MODULE    := libbase
LOCAL_SRC_FILES := ../../../../build/native/obj/local/armeabi/libbase.a

include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE    := libdalvikhook
LOCAL_SRC_FILES := ../../../../build/native/obj/local/armeabi/libdalvikhook.a

include $(PREBUILT_STATIC_LIBRARY)


include $(CLEAR_VARS)

LOCAL_MODULE    := librilinject
LOCAL_SRC_FILES := rilinject.c.arm
LOCAL_C_INCLUDES := ../../adbi/instruments/base/  ../../ddi/dalvikhook/jni/
LOCAL_LDLIBS	:= -llog
LOCAL_STATIC_LIBRARIES := libbase libdalvikhook
LOCAL_CFLAGS    := -g

include $(BUILD_SHARED_LIBRARY)
