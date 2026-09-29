// Real, minimal stand-in for the surrounding hand-written Kotlin/Android app's own bridge class
// (see stdlib/hw/usb_serial.prn's own header comment for the full real rationale). This sandbox
// has no com.hoho.android.usbserial jar and no Android SDK to compile the real thing against, so
// this stub proves the generated call shape (a static `port` field + `writeByte`/`readByte`
// static methods) matches what a real UsbSerialPort-backed bridge would expose -- it does NOT
// prove anything about the real Android library's own types or behavior.
public final class UsbSerialBridge {
    public static Object port = null;
    public static int writeByte(int b) { return port != null ? 1 : -1; }
    public static int readByte() { return port != null ? 0 : -1; }
}
