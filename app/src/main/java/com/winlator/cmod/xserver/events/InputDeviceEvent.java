package com.winlator.cmod.xserver.events;

import com.winlator.cmod.xconnector.XOutputStream;
import com.winlator.cmod.xserver.Bitmask;
import com.winlator.cmod.xserver.Window;

import java.io.IOException;
import java.nio.ByteBuffer;

public class InputDeviceEvent extends Event {
    private static final ByteBuffer eventBuf = ByteBuffer.allocateDirect(32);

    private final byte detail;
    private final int timestamp;
    private final Window root;
    private final Window event;
    private final Window child;
    private final short eventX;
    private final short eventY;
    private final short rootX;
    private final short rootY;
    private final Bitmask state;

    public InputDeviceEvent(int code, byte detail, Window root, Window event, Window child, short rootX, short rootY, short eventX, short eventY, Bitmask state) {
        super(code);
        this.detail = detail;
        this.timestamp = (int)System.currentTimeMillis();
        this.root = root;
        this.event = event;
        this.child = child;
        this.rootX = rootX;
        this.rootY = rootY;
        this.eventX = eventX;
        this.eventY = eventY;
        this.state = state;
    }

    @Override
    public void send(short sequenceNumber, XOutputStream outputStream) throws IOException {
        eventBuf.clear();
        eventBuf.order(outputStream.buffer.order());
        eventBuf.put((byte)code);
        eventBuf.put(detail);
        eventBuf.putShort(sequenceNumber);
        eventBuf.putInt(timestamp);
        eventBuf.putInt(root.id);
        eventBuf.putInt(event.id);
        eventBuf.putInt(child != null ? child.id : 0);
        eventBuf.putShort(rootX);
        eventBuf.putShort(rootY);
        eventBuf.putShort(eventX);
        eventBuf.putShort(eventY);
        eventBuf.putShort((short)state.getBits());
        eventBuf.put((byte)1);
        eventBuf.put((byte)0);
        eventBuf.flip();
        outputStream.clientSocket.write(eventBuf);
    }
}
