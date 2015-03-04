/**
 * Copyright (c) 2015 Joey Hewitt
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
 */
package net.scintill.rilextender;

import android.app.ActivityManager;
import android.app.IntentService;
import android.content.Context;
import android.content.Intent;
import android.util.Log;

import com.SecUpwN.AIMSICD.utils.CMDProcessor;
import com.SecUpwN.AIMSICD.utils.CommandResult;

import java.io.BufferedInputStream;
import java.io.BufferedOutputStream;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.StringReader;

public class RilExtenderInstaller extends IntentService {

    private static final String TAG = "RilExtender";

    private int mInjectedPid;
    private Context mContext;

    public RilExtenderInstaller() {
        super("RilExtenderInstaller");
        setIntentRedelivery(true);

        mInjectedPid = 0;
        mContext = this;
    }

    @Override
    protected void onHandleIntent(Intent intent) {
        boolean shouldInject = false;
        String libSuffix = "/librilinject.so";
        String libraryDir = mContext.getApplicationInfo().nativeLibraryDir;
        String libraryPath = libraryDir + libSuffix;

        // We'll remember if we've recently injected it, but maybe the app has restarted since
        // then, so check the actual running process.
        ActivityManager.RunningAppProcessInfo phoneProcInfo = getPhoneProcessInfo();
        if (phoneProcInfo == null) {
            throw new RuntimeException("unable to locate phone process");
        }
        int phonePid = phoneProcInfo.pid;
        int phoneUid = phoneProcInfo.uid;

        if (mInjectedPid == 0 || phonePid != mInjectedPid) {
            try {
                shouldInject = !checkIfLibraryAlreadyLoaded(phonePid, libSuffix);
            } catch (IOException e) {
                Log.e(TAG, "Error trying to determine if library is loaded. Not injecting.", e);
            }
        }

        if (shouldInject) {
            prepareDexFile(phoneUid);

            Log.d(TAG, "Installing RilExtender service");

            // XXX race conditions on loading it.. I guess dlopen()'s idempotent behavior makes it not too bad
            CommandResult result = CMDProcessor.runSuCommand(
                    libraryDir +"/lib__inject.bin__.so " + phonePid + " " + libraryPath);

            for (String output : new String[]{result.getStdout(), result.getStderr()}) {
                output = output.trim();
                if (output.length() != 0) {
                    Log.d(TAG, "[inject] " + output.replace("\n", "\n[inject] "));
                }
            }

            if (!result.success()) {
                throw new RuntimeException("unable to inject phone process: " + result);
            }

            mInjectedPid = phonePid;
        } else {
            Log.e(TAG, "Library was already injected.");
        }

    }

    private boolean checkIfLibraryAlreadyLoaded(int phonePid, String libSuffix) throws IOException {
        BufferedReader in = null;
        boolean sawStack = false, sawLib = false;
        try {
            String filePath = "/proc/"+phonePid+"/maps";
            CommandResult result = CMDProcessor.runSuCommand("cat "+filePath);
            if (!result.success()) {
                throw new IOException("error reading "+filePath);
            }

            in = new BufferedReader(new StringReader(result.getStdout()));
            String line;

            while ((line = in.readLine()) != null) {
                // sanity-check that we are reading correctly
                if (line.endsWith("[stack]")) {
                    sawStack = true;
                    if (sawLib) break;
                } else if (line.contains(libSuffix)) {
                    // this match is looser than I'd like, but it's to handle "lib.so (deleted)"
                    sawLib = true;
                    if (sawStack) break;
                }
            }
        } finally {
            if (in != null) in.close();
        }

        if (!sawStack) {
            throw new IOException("did not find stack; is the file being read wrong?");
        }

        return sawLib;
    }

    private void prepareDexFile(int phoneUid) {
        File rilExtenderDexCacheDir = mContext.getDir("rilextender-cache", Context.MODE_PRIVATE);
        File rilExtenderDex = new File(mContext.getDir("rilextender", Context.MODE_PRIVATE), "rilextender.dex");

        if (rilExtenderDex.getAbsolutePath().equals("/data/data/net.scintill.rilextender/app_rilextender/rilextender.dex") == false) {
            throw new RuntimeException("The dex wasn't placed where the hardcoded NDK injector expects it! Path was "+rilExtenderDex.getAbsolutePath());
            // We could probably have the NDK injector check several paths if this is a problem.
        }
        if (rilExtenderDexCacheDir.getAbsolutePath().equals("/data/data/net.scintill.rilextender/app_rilextender-cache") == false) {
            throw new RuntimeException("The dex cache wasn't placed where the hardcoded NDK injector expects it! Path was "+rilExtenderDexCacheDir.getAbsolutePath());
        }

        // TODO cache this?
        try {
            // Extract dex file from assets.
            // Thanks to https://github.com/creativepsyco/secondary-dex-gradle/blob/method2/app/src/main/java/com/github/creativepsyco/secondarydex/plugin/SecondaryDex.java
            // for the general idea.
            InputStream in = new BufferedInputStream(mContext.getAssets().open("rilextender.dex"));
            OutputStream out = new BufferedOutputStream(new FileOutputStream(rilExtenderDex));

            byte[] buf = new byte[1024];
            int len;
            while ((len = in.read(buf)) > 0) {
                out.write(buf, 0, len);
            }
            out.close(); // closes target too
            in.close(); // closes source too
        } catch (IOException e) {
            throw new RuntimeException("I/O error while extracting dex", e);
            // if the file is missing, the builder can try re-building the APK and run again
        }

        Log.d(TAG, rilExtenderDex.getName()+" extracted to "+rilExtenderDex.getAbsolutePath());

        // Make sure readable to the phone process.
        if (!rilExtenderDex.setReadable(true, false)) {
            throw new RuntimeException("chmod on dex failed");
        }

        // Can't be world-writable for security, and has to be writable by the phone
        // process (even if I dexopt it from here...)
        // If I put the dexopt'd file beside the .dex, with name .odex, it works; but
        // gives some warnings in the log, and is not really worth the trouble.
        // I also don't really like doing this every time, but I think the permissions could
        // change (for example, backup/restore or something), and I can't see a convenient way
        // to check the owner, so it's easiest to just set it every time.
        CMDProcessor.runSuCommand("chown "+phoneUid+":"+phoneUid+" /data/data/net.scintill.rilextender/app_rilextender-cache");
    }

    protected ActivityManager.RunningAppProcessInfo getPhoneProcessInfo() {
        ActivityManager am = (ActivityManager) mContext.getSystemService(Context.ACTIVITY_SERVICE);
        for (ActivityManager.RunningAppProcessInfo info : am.getRunningAppProcesses()) {
            if (info.processName.equalsIgnoreCase("com.android.phone")) {
                return info;
            }
        }

        return null;
    }

}
