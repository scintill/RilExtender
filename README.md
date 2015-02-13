# Ril Extender

The RilExtender is loaded in the running `com.android.phone` process's the main thread.
It installs a "service" exposing functions to callers with a custom Android permission `net.scintill.rilextender.RILEXTENDER_CLIENT` (which apparently includes `root`).

Since broadcasts were the only type of high-level IPC I could find, that didn't need to be
defined in the manifest, that's what we use to receive requests and return answers.  It's a bit
of a mess, so I may want to find something else.

## Example usage

The app currently shows no icon in the launcher. It's only accessible through Android service calls.  Here's how to use them from `adb shell` as `root`:

### Start the service (only needed once, unless the injected process is restarted)
    am startservice net.scintill.rilextender/.RilExtenderInstaller
    # If successful, a toast notification saying "RilExtender active" will appear in the upper-right corner of the screen. (Due to Superuser toasts blocking the display, it may take longer to show up than it actually takes to be ready..)
    # If it's already loaded, no toast is shown.
    
### Ping (check if it's alive)
    am broadcast -a net.scintill.rilextender.ping
    # Output: Broadcast completed: result=1, data="Bundle[{birthdate=1423787701642, version=11}]", extras: Bundle[mParcelledData.dataSize=76]
    # If not alive (applies to all functions): Broadcast completed: result=0
    
### SIM I/O
    am broadcast -a net.scintill.rilextender.iccio --ei command 192 --ei fileID 28542 --es path 3F007F20 --ei p1 0 --ei p2 0 --ei p3 15  --es data "" --es pin2 "" --es aid ""
    # Output: Broadcast completed: result=1, data="Bundle[return=XXXX]", extras: Bundle[mParcelledData.dataSize=56]
    
### oemRilRequestRaw - example for Qualcomm phones
    am broadcast -a net.scintill.rilextender.oemrilrequestraw --es argHex 514f454d484f4f4b13000800080000000100000001000000
    # Output: Broadcast completed: result=1, data="Bundle[return=]", extras: Bundle[mParcelledData.dataSize=36]
    # (Check `logcat -b radio` for responses)
    # Turn it off 
    am broadcast -a net.scintill.rilextender.oemrilrequestraw --es argHex 514f454d484f4f4b13000800080000000100000000000000
    
### oemRilRequestStrings - some phones might let you send AT commands with this
    am broadcast -a net.scintill.rilextender.oemrilrequeststrings --es arg ATI

### Error case: missing parameters
    am broadcast -a net.scintill.rilextender.iccio
    # Output: Broadcast completed: result=-1, data="expected int extra: command", extras: Bundle[mParcelledData.dataSize=96]

## Building

Something is wrong with the build process.  To reliably (re)build the injected dex, you have to build twice.  The first build doesn't properly package the secondary dex file in to the app.

## Logs

Example logcat command to filter to output from this service:

    adb logcat -s RilExtender,librilinject,CMDProcessor,lib__inject.bin__.so,System.err,su
