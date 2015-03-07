/*
 * Copyright (c) 2014 Joey Hewitt
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 *
 *
 *  Based on smsdispatch.c from:
 *  Collin's Dynamic Dalvik Instrumentation Toolkit for Android
 *  Collin Mulliner <collin[at]mulliner.org>
 *
 *  (c) 2012,2013
 *
 *  License: LGPL v2.1
 *
 */

// TODO review https://developer.android.com/training/articles/perf-jni.html , if this is going to be "production quality"
// TODO look into this logcat message: "W/linker  (27134): librilinject.so has text relocations. This is wasting memory and is a security risk. Please fix."
// TODO gracefully handle failures like permissions errors, instead of crashing the phone process? or maybe crashing is the easy way to clean up?

#define _GNU_SOURCE
#include <stdio.h>
#include <dlfcn.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <unistd.h>

#include "hook.h"
#include "dalvik_hook.h"
#include "dexstuff.h"
#include "base.h"

#include <jni.h>

#include <android/log.h>

static struct hook_t eph;
static struct dexstuff_t dexstuff;
static struct dalvik_hook_t dhs_newFromCMT;

static jclass class_RilExtender;

#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, "librilinject", __VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, "librilinject", __VA_ARGS__)
#if 1
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "librilinject", __VA_ARGS__)
#else
#define ALOGD(...)
#endif
static int my_log_debug = 1;
static void my_log(char *msg) { if(my_log_debug) ALOGD("%s", msg); }


static jclass loadClassFromDex(JNIEnv *env, const char *classNameSlash, const char *classNameDot, const char *dexPath, const char *cachePath) {
	jclass clTargetClass = (*env)->FindClass(env, classNameSlash);

	if (!clTargetClass) {
		(*env)->ExceptionClear(env); // FindClass() complains if there's an exception already

		// Load my class with BaseDexClassLoader
		// See RilExtenderCommandsInterface.java:
		// new BaseDexClassLoader(rilExtenderDex.getAbsolutePath(), rilExtenderDexCacheDir, null, ClassLoader.getSystemClassLoader())

		jclass clFile = (*env)->FindClass(env, "java/io/File");
		jmethodID mFileConstructor = (*env)->GetMethodID(env, clFile, "<init>", "(Ljava/lang/String;)V");
		jobject obCacheDirFile = NULL;
		if (clFile && mFileConstructor) {
			obCacheDirFile = (*env)->NewObject(env, clFile, mFileConstructor, (*env)->NewStringUTF(env, cachePath));
			if ((*env)->ExceptionCheck(env)) {
				ALOGE("new File() threw an exception");
				(*env)->ExceptionDescribe(env);
			}
		} else {
			ALOGE("Couldn't open cache File!");
		}

		jclass clDexClassLoader = (*env)->FindClass(env, "dalvik/system/BaseDexClassLoader");
		jmethodID mClassLoaderConstructor = (*env)->GetMethodID(env, clDexClassLoader, "<init>", "(Ljava/lang/String;Ljava/io/File;Ljava/lang/String;Ljava/lang/ClassLoader;)V");
		jmethodID mGetSystemClassLoader = (*env)->GetStaticMethodID(env, clDexClassLoader, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
		jmethodID mLoadClass = (*env)->GetMethodID(env, clDexClassLoader, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
		ALOGD("clDexClassLoader = %p, obCacheDirFile = %p", clDexClassLoader, obCacheDirFile);

		if (clDexClassLoader && mClassLoaderConstructor && mLoadClass && obCacheDirFile) {
			jobject classloaderobj = (*env)->NewObject(env, clDexClassLoader, mClassLoaderConstructor,
				(*env)->NewStringUTF(env, dexPath), obCacheDirFile, NULL,
				(*env)->CallStaticObjectMethod(env, clDexClassLoader, mGetSystemClassLoader));

			// XXX stingutf necesary?
			if (classloaderobj) {
				clTargetClass = (*env)->CallObjectMethod(env, classloaderobj, mLoadClass, (*env)->NewStringUTF(env, classNameDot));
				if ((*env)->ExceptionCheck(env)) {
					ALOGE("loadClass() threw an exception");
					(*env)->ExceptionDescribe(env);
				} else {
					// this is enough to get Dalvik to execute <clinit> (class static initialization block) for us
					(*env)->GetStaticMethodID(env, clTargetClass, "<clinit>", "()V");
			    }
			} else {
				ALOGE("classloader object not found!");
			}
		} else {
			ALOGE("classloader/constructor not found!");
		}

		ALOGD("clTargetClass = %p", clTargetClass);
	}

	return clTargetClass;
}

JNIEnv *findJniEnv(const char *pLibpath) {
	void *pLibVm = dlopen(pLibpath, RTLD_LAZY);

	if (!pLibpath) {
		return 0;
	}

	jint (*JNI_GetCreatedJavaVMs)(JavaVM**, jsize, jsize*) = dlsym(pLibVm, "JNI_GetCreatedJavaVMs");
	if (!JNI_GetCreatedJavaVMs) {
		ALOGE("error finding JNI_GetCreatedJavaVMs - %s", pLibpath);
		dlclose(pLibVm);
		return 0;
	}

	JavaVM *jvm;
	jsize sz;

	if (JNI_GetCreatedJavaVMs(&jvm, 1, &sz) != JNI_OK) {
		ALOGE("error in JNI_GetCreatedJavaVMs - %s", pLibpath);
		dlclose(pLibVm);
		return 0;
	}
	if (sz == 0 || !jvm) {
		ALOGE("didn't find a VM - %s", pLibpath);
		dlclose(pLibVm);
		return 0;
	}

	JNIEnv *env;
	if ((*jvm)->GetEnv(jvm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) {
		ALOGE("error in JVM::GetEnv - %s", pLibpath);
		dlclose(pLibVm);
		return 0;
	}

	return env;
}

static jobject my_newFromCMT(JNIEnv *env, jclass clazz, jobjectArray jasLines) {
	// call original method
	dalvik_prepare(&dexstuff, &dhs_newFromCMT, env);
	(*env)->ExceptionClear(env);
	jobject returnedSmsMessage = (*env)->CallStaticObjectMethod(env, clazz, dhs_newFromCMT.mid, jasLines);
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionDescribe(env);
	}
	/*ALOGD("success calling : newFromCMT. returned=%p", returnedSmsMessage);*/
	dalvik_postcall(&dexstuff, &dhs_newFromCMT);

	static jmethodID method_onNewFromCMT = NULL;
	if (!method_onNewFromCMT) {
		(*env)->ExceptionClear(env);
		method_onNewFromCMT = (*env)->GetStaticMethodID(env, class_RilExtender, "onNewFromCMT", "(Ljava/lang/Object;)V");
		if ((*env)->ExceptionCheck(env)) {
			(*env)->ExceptionDescribe(env);
		}
	}

	if (method_onNewFromCMT) {
		(*env)->ExceptionClear(env);
		(*env)->CallStaticVoidMethod(env, class_RilExtender, method_onNewFromCMT, returnedSmsMessage);
		if ((*env)->ExceptionCheck(env)) {
			(*env)->ExceptionDescribe(env);
		}
	} else {
		ALOGE("onNewFromCMT() not found!");
	}

	(*env)->ExceptionClear(env);
	return returnedSmsMessage;
}

static int my_epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout) {
#define RETURN() return orig_epoll_wait(epfd, events, maxevents, timeout);

	ALOGI("my_epoll_wait()");

	int (*orig_epoll_wait)(int epfd, struct epoll_event *events, int maxevents, int timeout);
	orig_epoll_wait = (void*)eph.orig;
	// remove hook for epoll_wait
	hook_precall(&eph);

	// find JNI env
	JNIEnv *env = findJniEnv("libdvm.so");
	bool isDalvik = true;
	if (!env) {
		env = findJniEnv("libart.so");
		if (!env) {
			ALOGE("error getting JNIEnv");
			RETURN();
		}
		isDalvik = false;
	}

	// load Java code
	class_RilExtender = loadClassFromDex(env, "net/scintill/rilextender/RilExtender", "net.scintill.rilextender.RilExtender",
		"/data/data/net.scintill.rilextender/app_rilextender/rilextender.dex", "/data/data/net.scintill.rilextender/app_rilextender-cache");

	if (!class_RilExtender) {
		ALOGE("error in loadClassFromDex");
		RETURN();
	} else {
		// hold this reference. XXX no releasing, not necessary for now
		class_RilExtender = (*env)->NewGlobalRef(env, class_RilExtender);
	}

	bool supportsRawSmsPdu = false; // TODO pass this in to RilExtender class

	if (isDalvik) {
		dalvikhook_set_logfunction(my_log);

		// resolve symbols from DVM
		my_log_debug = 0; // dlopen logging is noisy
		dexstuff_resolv_dvm(&dexstuff);
		my_log_debug = 1;

		// hook functions
		dalvik_hook_setup(&dhs_newFromCMT, "Lcom/android/internal/telephony/gsm/SmsMessage;", "newFromCMT", "([Ljava/lang/String;)Lcom/android/internal/telephony/gsm/SmsMessage;", 1, my_newFromCMT);
		//dhs_newFromCMT.debug_me = 1;
		if (dalvik_hook(&dexstuff, &dhs_newFromCMT)) {
			ALOGD("hooked SmsMessage#newFromCMT()");
			supportsRawSmsPdu = true;
		} else {
			ALOGE("hooking SmsMessage#newFromCMT() failed");
		}
	} else {
		ALOGE("skipping Dalvik hooks, because not Dalvik");
	}


	RETURN();

#undef RETURN
}

// entry point when this library is loaded
void __attribute__ ((constructor)) my_init() {
	ALOGI("my_init()");

	// set log function for libbase
	set_logfunction(my_log);

	// If I try to load the class directly here, I get
	// "Optimized data directory /data/data/net.scintill.rilextender/app_rilextender-cache is not owned by the current user.
	// Shared storage cannot protect your application from code injection attacks.", despite having taken care to set that ownership
	// correctly.  The backtrace starts at android.os.MessageQueue.nativePollOnce. I guess epoll_wait was chosen by
	// Collin Mulliner as a sane-ish place in the call stack to do crazy stuff like this.
	hook(&eph, getpid(), "libc.", "epoll_wait", my_epoll_wait, 0);
}
