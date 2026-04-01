package com.github.mowerick.ros2.android.util

import java.util.concurrent.TimeUnit
import java.util.concurrent.locks.ReentrantLock
import kotlin.concurrent.withLock
import kotlin.math.min

/**
 * Thread-safe circular byte buffer for buffering serial data.
 *
 * This buffer is used by BufferedUsbSerialPort to store data read from USB in a background thread,
 * allowing the native YDLIDAR SDK to query available bytes and read with timeout without blocking
 * USB operations.
 *
 * Features:
 * - Thread-safe read/write operations using ReentrantLock
 * - Blocking read with timeout support
 * - Overwrites oldest data when buffer is full
 * - O(1) available() query
 *
 * @param capacity Maximum buffer size in bytes (default: 16KB for YDLIDAR bursts)
 */
class CircularByteBuffer(private val capacity: Int = 16384) {

    private val buffer = ByteArray(capacity)
    private var head = 0  // Read position
    private var tail = 0  // Write position
    private var count = 0 // Number of bytes available

    private val lock = ReentrantLock()
    private val notEmpty = lock.newCondition()

    /**
     * Get number of bytes available for reading
     *
     * @return Number of bytes currently in buffer
     */
    fun available(): Int = lock.withLock { count }

    /**
     * Write data to buffer (non-blocking)
     *
     * If buffer is full, oldest data will be overwritten.
     * Signals waiting readers when data is added.
     *
     * @param src Source byte array
     * @param offset Offset in source array
     * @param length Number of bytes to write
     */
    fun write(src: ByteArray, offset: Int, length: Int) {
        lock.withLock {
            var remaining = length
            var srcPos = offset

            while (remaining > 0) {
                // Calculate how many bytes to write in this iteration
                // (limited by space until end of buffer)
                val toWrite = min(remaining, capacity - tail)

                // Copy data to buffer
                System.arraycopy(src, srcPos, buffer, tail, toWrite)

                // Advance tail (wrap around if needed)
                tail = (tail + toWrite) % capacity

                // Update count (capped at capacity - overwrites old data)
                val oldCount = count
                count = min(count + toWrite, capacity)

                // If we overwrote data, advance head
                if (count == capacity && oldCount + toWrite > capacity) {
                    head = tail
                }

                srcPos += toWrite
                remaining -= toWrite
            }

            // Signal waiting readers that data is available
            notEmpty.signal()
        }
    }

    /**
     * Read data from buffer (blocking with timeout)
     *
     * Blocks until at least 1 byte is available or timeout expires.
     * May return fewer bytes than requested if timeout occurs or buffer empties.
     *
     * @param dest Destination byte array
     * @param offset Offset in destination array
     * @param length Maximum number of bytes to read
     * @param timeoutMs Timeout in milliseconds (0 = non-blocking, <0 = wait forever)
     * @return Number of bytes actually read (0 if timeout)
     */
    fun read(dest: ByteArray, offset: Int, length: Int, timeoutMs: Long): Int {
        val deadline = if (timeoutMs >= 0) {
            System.currentTimeMillis() + timeoutMs
        } else {
            Long.MAX_VALUE
        }

        lock.withLock {
            // Wait for data if buffer is empty
            while (count == 0) {
                if (timeoutMs == 0L) {
                    // Non-blocking mode
                    return 0
                }

                val remaining = deadline - System.currentTimeMillis()
                if (remaining <= 0) {
                    // Timeout expired
                    return 0
                }

                // Wait for data with timeout
                if (!notEmpty.await(remaining, TimeUnit.MILLISECONDS)) {
                    // Timeout during wait
                    return 0
                }
            }

            // Read available data (up to requested length)
            val toRead = min(length, count)
            var remaining = toRead
            var destPos = offset

            while (remaining > 0) {
                // Calculate how many bytes to read in this iteration
                val chunk = min(remaining, capacity - head)

                // Copy data from buffer
                System.arraycopy(buffer, head, dest, destPos, chunk)

                // Advance head (wrap around if needed)
                head = (head + chunk) % capacity

                // Update count
                count -= chunk

                destPos += chunk
                remaining -= chunk
            }

            return toRead
        }
    }

    /**
     * Clear all data from buffer
     *
     * Resets buffer to empty state.
     */
    fun clear() {
        lock.withLock {
            head = 0
            tail = 0
            count = 0
        }
    }

    /**
     * Get buffer statistics (for debugging)
     */
    fun getStats(): String {
        return lock.withLock {
            "CircularByteBuffer(capacity=$capacity, available=$count, head=$head, tail=$tail)"
        }
    }
}
