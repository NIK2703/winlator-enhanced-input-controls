package com.winlator.cmod.xconnector;

import androidx.annotation.Keep;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.ArrayDeque;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicInteger;

public class ClientSocket {
    public final int fd;
    private final ArrayDeque<Integer> ancillaryFds = new ArrayDeque<>();

    private static final int MAX_PENDING_BYTES = 1024 * 1024;
    private final ConcurrentLinkedQueue<ByteBuffer> writeQueue = new ConcurrentLinkedQueue<>();
    private final AtomicInteger pendingBytes = new AtomicInteger(0);
    private final Object writeSignal = new Object();
    private volatile boolean running = true;
    private volatile boolean writeQueueOverflow = false;
    private Thread writerThread;

    static {
        System.loadLibrary("winlator");
    }

    public ClientSocket(int fd) {
        this.fd = fd;
        setNonBlocking(fd);
        startWriterThread();
    }

    private void startWriterThread() {
        writerThread = new Thread(this::writeLoop, "socket-writer-" + fd);
        writerThread.setDaemon(true);
        writerThread.start();
    }

    private volatile long diagWriteCount = 0;
    private volatile long diagSlowWrites = 0;
    private volatile long diagBlockedWrites = 0;

    private void writeLoop() {
        ByteBuffer buf;
        while (running) {
            buf = writeQueue.poll();
            if (buf == null) {
                synchronized (writeSignal) {
                    try {
                        writeSignal.wait(100);
                    }
                    catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        return;
                    }
                }
                continue;
            }

            int offset = 0;
            int remaining = buf.remaining();
            long writeStart = System.nanoTime();
            while (remaining > 0 && running) {
                int written = write(fd, buf, offset, remaining);
                if (written > 0) {
                    offset += written;
                    remaining -= written;
                }
                else if (written == 0) {
                    diagBlockedWrites++;
                    try {
                        Thread.sleep(1);
                    }
                    catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        return;
                    }
                }
                else {
                    break;
                }
            }
            long writeElapsedUs = (System.nanoTime() - writeStart) / 1000;
            diagWriteCount++;
            if (writeElapsedUs > 10000) {
                diagSlowWrites++;
            }
            pendingBytes.addAndGet(-(buf.limit() - remaining));
        }
    }

    public void shutdown() {
        running = false;
        synchronized (writeSignal) {
            writeSignal.notify();
        }
    }

    static void writeDirect(int fd, ByteBuffer data) {
        int offset = 0;
        int remaining = data.remaining();
        long deadline = System.nanoTime() + 5_000_000_000L;
        while (remaining > 0) {
            if (System.nanoTime() > deadline) {
                return;
            }
            int written = write(fd, data, offset, remaining);
            if (written > 0) {
                offset += written;
                remaining -= written;
            }
            else if (written == 0) {
                try {
                    Thread.sleep(1);
                }
                catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
            else return;
        }
    }

    public boolean hasAncillaryFds() {
        return !ancillaryFds.isEmpty();
    }

    public int getAncillaryFd() {
        return hasAncillaryFds() ? ancillaryFds.poll() : -1;
    }

    @Keep
    public void addAncillaryFd(int ancillaryFd) {
        ancillaryFds.add(ancillaryFd);
    }

    public int read(ByteBuffer data) throws IOException {
        int position = data.position();
        int bytesRead = read(fd, data, position, data.remaining());
        if (bytesRead > 0) {
            data.position(position + bytesRead);
            return bytesRead;
        }
        else if (bytesRead == 0) {
            return -1;
        }
        else throw new IOException("Failed to read data.");
    }

    public void write(ByteBuffer data) throws IOException {
        int remaining = data.remaining();
        if (remaining == 0) return;

        while (pendingBytes.get() + remaining > MAX_PENDING_BYTES) {
            ByteBuffer dropped = writeQueue.poll();
            if (dropped == null) break;
            pendingBytes.addAndGet(-dropped.limit());
            writeQueueOverflow = true;
        }

        ByteBuffer copy = ByteBuffer.allocateDirect(remaining);
        copy.put(data);
        copy.flip();

        writeQueue.add(copy);
        pendingBytes.addAndGet(remaining);

        synchronized (writeSignal) {
            writeSignal.notify();
        }
    }

    public boolean hasOverflowed() {
        return writeQueueOverflow;
    }

    public void resetOverflowFlag() {
        writeQueueOverflow = false;
    }

    public int recvAncillaryMsg(ByteBuffer data) throws IOException {
        int position = data.position();
        int bytesRead = recvAncillaryMsg(fd, data, position, data.remaining());
        if (bytesRead > 0) {
            data.position(position + bytesRead);
            return bytesRead;
        }
        else if (bytesRead == 0) {
            return -1;
        }
        else throw new IOException("Failed to receive ancillary messages.");
    }

    public void sendAncillaryMsg(ByteBuffer data, int ancillaryFd) throws IOException {
        int bytesSent = sendAncillaryMsg(fd, data, data.limit(), ancillaryFd);
        if (bytesSent >= 0) {
            data.position(bytesSent);
        }
        else throw new IOException("Failed to send ancillary messages.");
    }

    private static native int setNonBlocking(int fd);

    private native int read(int fd, ByteBuffer data, int offset, int length);

    private static native int write(int fd, ByteBuffer data, int offset, int length);

    private native int recvAncillaryMsg(int clientFd, ByteBuffer data, int offset, int length);

    private native int sendAncillaryMsg(int clientFd, ByteBuffer data, int length, int ancillaryFd);
}
