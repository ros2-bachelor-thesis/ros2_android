package com.github.mowerick.ros2.android

import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialPort
import java.io.IOException

/**
 * Buffered wrapper around UsbSerialPort with background reader thread.
 *
 * This class solves the problem that mik3y/usb-serial-for-android does not provide an `available()`
 * method to check how many bytes are waiting. The YDLIDAR SDK relies heavily on `ioctl(FIONREAD)`
 * to query available bytes before reading.
 *
 * Solution:
 * - Background thread continuously reads from USB into a CircularByteBuffer
 * - JNI code queries buffer for available bytes (instant, no USB I/O)
 * - JNI code reads from buffer with timeout (blocks on buffer, not USB)
 *
 * Thread Safety:
 * - All operations are thread-safe via CircularByteBuffer's internal locking
 * - Reader thread runs at MAX_PRIORITY for real-time data collection
 * - Proper shutdown ensures no data loss or thread leaks
 *
 * @param port UsbSerialPort instance (from mik3y library)
 * @param bufferSize Ring buffer size in bytes (default: 16KB)
 */
class BufferedUsbSerialPort(
    private val port: UsbSerialPort,
    bufferSize: Int = 16384
) : AutoCloseable {

    private val buffer = CircularByteBuffer(bufferSize)
    private val readerThread: Thread

    @Volatile
    private var stopped = false

    @Volatile
    private var lastError: IOException? = null

    init {
        // Start background reader thread
        readerThread = Thread({
            readerThreadLoop()
        }, "UsbSerialReader-${port.device.deviceName}").apply {
            // High priority for real-time data collection
            priority = Thread.MAX_PRIORITY
            isDaemon = true
            start()
        }

        Log.i(TAG, "BufferedUsbSerialPort started for ${port.device.deviceName}")
    }

    /**
     * Background reader thread main loop
     *
     * Continuously reads chunks from USB and writes to circular buffer.
     * Uses 100ms timeout to balance responsiveness and CPU usage.
     */
    private fun readerThreadLoop() {
        val chunk = ByteArray(CHUNK_SIZE)

        try {
            while (!stopped) {
                try {
                    // Read chunk from USB (100ms timeout)
                    val n = port.read(chunk, READ_TIMEOUT_MS)

                    if (n > 0) {
                        // Write to circular buffer (non-blocking, overwrites old data if full)
                        buffer.write(chunk, 0, n)
                        Log.v(TAG, "Read $n bytes from USB, buffer now has ${buffer.available()} bytes")
                    }
                } catch (e: IOException) {
                    if (!stopped) {
                        Log.e(TAG, "USB read error: ${e.message}", e)
                        lastError = e

                        // Check if connection is closed - if so, exit immediately
                        if (e.message?.contains("Connection closed") == true) {
                            Log.w(TAG, "USB connection closed, exiting reader thread")
                            break
                        }

                        // Continue reading - other USB errors may be transient
                    }
                }
            }
        } finally {
            Log.i(TAG, "Reader thread exiting for ${port.device.deviceName}")
        }
    }

    /**
     * Get number of bytes available for reading from buffer
     *
     * This is called frequently by YDLIDAR SDK via JNI to check if enough data
     * has arrived before attempting a read.
     *
     * @return Number of bytes currently in buffer
     */
    fun available(): Int {
        return buffer.available()
    }

    /**
     * Read data from buffer (blocking with timeout)
     *
     * Called by YDLIDAR SDK via JNI when it needs to read serial data.
     * Blocks until data is available or timeout expires.
     *
     * @param dest Destination byte array
     * @param timeoutMs Timeout in milliseconds
     * @return Number of bytes actually read (may be less than dest.size)
     * @throws IOException if port was closed or error occurred
     */
    fun read(dest: ByteArray, timeoutMs: Int): Int {
        checkError()

        val bytesRead = buffer.read(dest, 0, dest.size, timeoutMs.toLong())

        if (bytesRead > 0) {
            Log.v(TAG, "Read $bytesRead bytes from buffer")
        }

        return bytesRead
    }

    /**
     * Write data directly to USB port
     *
     * Writes bypass the buffer and go straight to USB.
     * This is intentional - YDLIDAR sends commands infrequently.
     *
     * @param src Source byte array
     * @param timeoutMs Timeout in milliseconds
     * @throws IOException if write fails or port is closed
     */
    fun write(src: ByteArray, timeoutMs: Int) {
        checkError()

        try {
            port.write(src, timeoutMs)
            Log.v(TAG, "Wrote ${src.size} bytes to USB")
        } catch (e: IOException) {
            Log.e(TAG, "USB write error: ${e.message}", e)
            lastError = e
            throw e
        }
    }

    /**
     * Flush buffers
     *
     * @param input If true, clear input buffer (circular buffer)
     * @param output If true, purge USB hardware output buffer
     */
    fun flush(input: Boolean, output: Boolean) {
        if (input) {
            buffer.clear()
            Log.d(TAG, "Cleared input buffer")
        }

        if (output) {
            try {
                port.purgeHwBuffers(false, true)
                Log.d(TAG, "Purged USB output buffer")
            } catch (e: IOException) {
                Log.w(TAG, "Failed to purge USB buffers: ${e.message}")
            }
        }
    }

    /**
     * Check if an error occurred in reader thread
     *
     * @throws IOException if reader thread encountered error
     */
    private fun checkError() {
        lastError?.let {
            throw IOException("USB port error: ${it.message}", it)
        }
    }

    /**
     * Close port and stop reader thread
     *
     * Blocks until reader thread exits (max 1 second).
     */
    override fun close() {
        if (stopped) {
            return
        }

        Log.i(TAG, "Closing BufferedUsbSerialPort for ${port.device.deviceName}")

        stopped = true

        // Wait for reader thread to exit
        try {
            readerThread.join(1000)
            if (readerThread.isAlive) {
                Log.w(TAG, "Reader thread did not exit cleanly")
            }
        } catch (e: InterruptedException) {
            Log.w(TAG, "Interrupted while waiting for reader thread")
        }

        // Close underlying USB port
        try {
            port.close()
            Log.i(TAG, "USB port closed")
        } catch (e: IOException) {
            Log.e(TAG, "Error closing USB port: ${e.message}", e)
        }
    }

    /**
     * Get buffer statistics (for debugging)
     */
    fun getBufferStats(): String {
        return buffer.getStats()
    }

    companion object {
        private const val TAG = "BufferedUsbSerialPort"

        // Reader thread configuration
        private const val CHUNK_SIZE = 4096        // Read 4KB chunks from USB
        private const val READ_TIMEOUT_MS = 100    // 100ms timeout for USB reads
    }
}
