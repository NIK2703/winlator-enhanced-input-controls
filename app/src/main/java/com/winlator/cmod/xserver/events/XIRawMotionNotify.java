package com.winlator.cmod.xserver.events;

import android.os.SystemClock;

import com.winlator.cmod.xconnector.XOutputStream;
import com.winlator.cmod.xconnector.XStreamLock;
import com.winlator.cmod.xserver.Window;

import java.io.IOException;

public class XIRawMotionNotify extends Event {
    public static final int GENERIC_EVENT_CODE = 35;
    private static final short XI_RAWMOTION_EVTYPE = 17;

    private final byte extensionOpcode;
    private final int deviceId;
    private final double[] rawValues;
    private final double[] deltaValues;
    private final int valuatorMask;

    public XIRawMotionNotify(int deviceId,
                             byte extensionOpcode,
                             double[] rawValues,
                             double[] deltaValues,
                             int valuatorMask) {
        super(GENERIC_EVENT_CODE);
        this.deviceId = deviceId;
        this.extensionOpcode = extensionOpcode;
        this.rawValues = rawValues;
        this.deltaValues = deltaValues;
        this.valuatorMask = valuatorMask;
    }

    @Override
    public void send(short sequenceNumber, XOutputStream outputStream) throws IOException {
        try (XStreamLock lock = outputStream.lock()) {

            short maskLenUnits = 1;

            int numAxes = rawValues.length;
            int payloadBytes =
                4 +                   
                (numAxes * 8) +       
                (numAxes * 8);         

            int payloadLengthUnits = payloadBytes / 4;

            outputStream.writeByte(this.code);               
            outputStream.writeByte(extensionOpcode);         
            outputStream.writeShort(sequenceNumber);         
            outputStream.writeInt(payloadLengthUnits);       
            outputStream.writeShort(XI_RAWMOTION_EVTYPE);    
            outputStream.writeShort((short) deviceId);      
            outputStream.writeInt((int) SystemClock.uptimeMillis()); 
            outputStream.writeInt(0);                       
            outputStream.writeShort((short) deviceId);    
            outputStream.writeShort(maskLenUnits);           
            outputStream.writeInt(0);                       
            outputStream.writePad(4);                        

            outputStream.writeInt(valuatorMask);

            for (double v : rawValues) {
                outputStream.writeFP3232(v);
            }

            for (double v : deltaValues) {
                outputStream.writeFP3232(v);
            }
        }
    }
}
