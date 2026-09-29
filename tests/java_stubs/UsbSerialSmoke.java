// Real, hermetic smoke test for stdlib/hw/usb_serial.prn's generated Java, run against
// UsbSerialBridge.java's own stub (see that file's header for what this does and doesn't prove).
// Exercises both the not-connected and connected paths, plus the pure-scalar baud-table decision.
public class UsbSerialSmoke {
    public static void main(String[] args) {
        int pass = 0, fail = 0;

        if (!UsbSerial.usbSerialIsConnected()) pass++; else fail++;
        if (UsbSerial.usbSerialWriteByte(65) == -1) pass++; else fail++;
        if (UsbSerial.usbSerialReadByte() == -1) pass++; else fail++;

        if (UsbSerial.usbSerialBaudForBoard(0) == 115200) pass++; else fail++;
        if (UsbSerial.usbSerialBaudForBoard(1) == 57600) pass++; else fail++;
        if (UsbSerial.usbSerialBaudForBoard(2) == 57600) pass++; else fail++;

        UsbSerialBridge.port = new Object();
        if (UsbSerial.usbSerialIsConnected()) pass++; else fail++;
        if (UsbSerial.usbSerialWriteByte(65) == 1) pass++; else fail++;

        System.out.println(pass + " passed, " + fail + " failed");
        System.exit(fail > 0 ? 1 : 0);
    }
}
