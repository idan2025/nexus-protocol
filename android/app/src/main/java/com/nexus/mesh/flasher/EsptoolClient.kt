package com.nexus.mesh.flasher

import android.util.Log
import com.hoho.android.usbserial.driver.UsbSerialPort
import java.io.IOException

/**
 * Minimal Kotlin port of esptool's ROM-loader protocol — enough to
 * write a single merged image to flash on ESP32-S3 boards.
 *
 * Protocol references:
 *   https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/serial-protocol.html
 *
 * We use the SLOW path (no stub loader) so we don't have to bundle a
 * stub binary. For a 700 KB merged image this works out to roughly
 * 60–120 seconds of wall time at 115200 baud — acceptable for a
 * one-shot phone-side flash.
 */
class EsptoolClient(private val port: UsbSerialPort) {

    fun close() {
        try { port.close() } catch (_: Exception) {}
    }

    /**
     * Send the SYNC command repeatedly. Returns true if the chip
     * responds (it's now in ROM bootloader and ready to accept
     * commands).
     *
     * Real esptool retries for several seconds per "connection attempt"
     * — the ROM boot banner (`ESP-ROM:esp32s3-...`) takes ~100 ms to
     * print after a reset and the bootloader's own SYNC poll window is
     * narrow, so a 700 ms cap (the previous default) consistently
     * missed it on phones where USB writes are delayed by 40-80 ms.
     */
    fun sync(maxAttempts: Int = 10): Boolean {
        // Clear the bootloader's boot banner and any leftover bytes
        // before sending sync. Two passes: first an aggressive flush
        // until the pipe is quiet for ~80 ms, then a hw purge if the
        // driver supports it.
        drainUntilQuiet(quietMs = 80, hardCapMs = 400)
        try { port.purgeHwBuffers(true, true) } catch (_: Exception) {}

        val syncPayload = ByteArray(4 + 32).apply {
            this[0] = 0x07; this[1] = 0x07; this[2] = 0x12; this[3] = 0x20
            for (i in 4 until size) this[i] = 0x55
        }
        repeat(maxAttempts) {
            try {
                sendCommand(CMD_SYNC, syncPayload, 0)
                // SYNC responds *eight* times — we only need to see one
                // valid frame to know it's there. Give the chip enough
                // wall-time to actually answer; ROM SYNC ack latency on
                // Heltec V3 is 50-300 ms cold.
                val resp = readResponse(timeoutMs = 500, expectedCmd = CMD_SYNC)
                if (resp != null) {
                    drainUntilQuiet(quietMs = 50, hardCapMs = 200)
                    return true
                }
            } catch (_: Exception) {}
        }
        return false
    }

    /**
     * Erase the region we're about to overwrite and queue [size] bytes
     * worth of data blocks at [offset].
     */
    fun flashBegin(size: Int, offset: Int, blockSize: Int = FLASH_BLOCK_SIZE) {
        val numBlocks = (size + blockSize - 1) / blockSize
        // ESP32-S3 ROM uses the 16-byte FLASH_BEGIN form (no encrypted flag).
        val payload = packLeInts(size, numBlocks, blockSize, offset)
        sendCommand(CMD_FLASH_BEGIN, payload, 0)
        readResponse(timeoutMs = 30_000, expectedCmd = CMD_FLASH_BEGIN)
            ?: throw IOException("FLASH_BEGIN: no response (chip in download mode?)")
    }

    /** Send one block to flash. */
    fun flashData(block: ByteArray, sequence: Int) {
        require(block.size <= FLASH_BLOCK_SIZE) { "block too large" }
        // Pad with 0xFF so the bootloader sees a full block.
        val padded = if (block.size == FLASH_BLOCK_SIZE) block else
            block + ByteArray(FLASH_BLOCK_SIZE - block.size) { 0xFF.toByte() }
        var checksum = 0xEF
        for (b in padded) checksum = checksum xor (b.toInt() and 0xFF)
        val header = packLeInts(padded.size, sequence, 0, 0)
        sendCommand(CMD_FLASH_DATA, header + padded, checksum)
        readResponse(timeoutMs = 5_000, expectedCmd = CMD_FLASH_DATA)
            ?: throw IOException("FLASH_DATA seq=$sequence: no ack")
    }

    /**
     * Full chip erase via the ROM bootloader. ROM has no native
     * ESP_ERASE_FLASH (0xD0) -- that's a stub-only command. Instead we
     * piggyback on FLASH_BEGIN's region-erase side effect: the ROM
     * erases the entire [offset, offset+size) range up front before
     * accepting any FLASH_DATA. By issuing FLASH_BEGIN with
     * numBlocks=0 over the full flash size and then closing the
     * (empty) write session with FLASH_END, we get a clean chip erase
     * without needing to upload a stub binary.
     *
     * Takes roughly 5 s / MB on ESP32-S3, so ~40 s for 8 MB. The ROM
     * only acks the FLASH_BEGIN once erase is finished, hence the
     * 90 s timeout.
     */
    fun eraseFlash(flashSizeBytes: Int = 8 * 1024 * 1024) {
        Log.i(TAG, "Erasing $flashSizeBytes bytes — this takes ~5 s/MB")
        val payload = packLeInts(flashSizeBytes, 0, FLASH_BLOCK_SIZE, 0)
        sendCommand(CMD_FLASH_BEGIN, payload, 0)
        readResponse(timeoutMs = 90_000, expectedCmd = CMD_FLASH_BEGIN)
            ?: throw IOException("ERASE_FLASH: no response after 90 s")
        flashEnd(reboot = false)
    }

    /** Tell the bootloader we're done. flag=0 -> reboot into the new firmware. */
    fun flashEnd(reboot: Boolean = true) {
        sendCommand(CMD_FLASH_END, packLeInts(if (reboot) 0 else 1), 0)
        // Don't wait for an ack on reboot — the chip restarts and the
        // serial port is closed by the host immediately.
        if (!reboot) readResponse(timeoutMs = 1_000, expectedCmd = CMD_FLASH_END)
    }

    /**
     * Write a full image. Calls [progress] with (sequence, totalBlocks)
     * after each block.
     */
    fun writeImage(
        image: ByteArray,
        offset: Int = 0,
        progress: (Int, Int) -> Unit = { _, _ -> }
    ) {
        flashBegin(image.size, offset)
        val total = (image.size + FLASH_BLOCK_SIZE - 1) / FLASH_BLOCK_SIZE
        var seq = 0
        var pos = 0
        while (pos < image.size) {
            val end = minOf(pos + FLASH_BLOCK_SIZE, image.size)
            val block = image.copyOfRange(pos, end)
            flashData(block, seq)
            progress(seq + 1, total)
            seq++
            pos = end
        }
        flashEnd(reboot = true)
    }

    /* ── Internal: framing ─────────────────────────────────────────── */

    private fun sendCommand(cmd: Byte, data: ByteArray, checksum: Int) {
        // Command frame: [direction(1)=0x00][cmd(1)][size(2,LE)][checksum(4,LE)][payload]
        val frame = ByteArray(8 + data.size)
        frame[0] = 0x00
        frame[1] = cmd
        frame[2] = (data.size and 0xFF).toByte()
        frame[3] = ((data.size shr 8) and 0xFF).toByte()
        frame[4] = (checksum and 0xFF).toByte()
        frame[5] = ((checksum shr 8) and 0xFF).toByte()
        frame[6] = ((checksum shr 16) and 0xFF).toByte()
        frame[7] = ((checksum shr 24) and 0xFF).toByte()
        System.arraycopy(data, 0, frame, 8, data.size)
        val slip = slipEncode(frame)
        port.write(slip, 2_000)
    }

    private fun readResponse(timeoutMs: Int, expectedCmd: Byte): Frame? {
        val buf = ByteArray(512)
        val deadline = System.currentTimeMillis() + timeoutMs
        val accumulator = ArrayList<Byte>(256)
        // Read until we find a complete SLIP frame (between two 0xC0).
        while (System.currentTimeMillis() < deadline) {
            val remaining = (deadline - System.currentTimeMillis()).toInt().coerceAtLeast(50)
            val n = try { port.read(buf, remaining) } catch (e: Exception) { -1 }
            if (n <= 0) continue
            for (i in 0 until n) accumulator.add(buf[i])
            // Try to extract a frame.
            val frame = tryExtractFrame(accumulator) ?: continue
            // Frame is: [0x01][cmd][size(2)][value(4)][payload][status(1)][reason(1)]
            if (frame.size < 10 || frame[0] != 0x01.toByte()) continue
            if (frame[1] != expectedCmd) continue
            val sz = (frame[2].toInt() and 0xFF) or ((frame[3].toInt() and 0xFF) shl 8)
            return Frame(cmd = frame[1], size = sz, value = readLeInt(frame, 4),
                         payload = frame.copyOfRange(8, 8 + sz))
        }
        return null
    }

    private fun tryExtractFrame(buf: ArrayList<Byte>): ByteArray? {
        // Find first 0xC0
        var start = -1
        for (i in buf.indices) {
            if (buf[i] == 0xC0.toByte()) { start = i; break }
        }
        if (start < 0) {
            buf.clear(); return null
        }
        // Find next 0xC0 after start.
        var end = -1
        for (i in start + 1 until buf.size) {
            if (buf[i] == 0xC0.toByte()) { end = i; break }
        }
        if (end < 0) {
            // Drop bytes before start; wait for more data.
            for (i in 0 until start) buf.removeAt(0)
            return null
        }
        val raw = buf.subList(start + 1, end).toByteArray()
        // Drop everything up to and including the closing 0xC0
        val toRemove = end + 1
        repeat(toRemove) { buf.removeAt(0) }
        return slipDecode(raw)
    }

    private fun drain() {
        val tmp = ByteArray(256)
        try { port.read(tmp, 50) } catch (_: Exception) {}
    }

    /**
     * Keep reading until the pipe has been quiet for [quietMs] ms or
     * we've spent [hardCapMs] ms total. Used to clear the ROM boot
     * banner before SYNC — a single short read isn't enough on phones
     * where USB events arrive in bursts.
     */
    private fun drainUntilQuiet(quietMs: Int, hardCapMs: Int) {
        val tmp = ByteArray(512)
        val hardDeadline = System.currentTimeMillis() + hardCapMs
        var lastDataAt = System.currentTimeMillis()
        while (System.currentTimeMillis() < hardDeadline) {
            val n = try { port.read(tmp, 25) } catch (_: Exception) { 0 }
            if (n > 0) {
                lastDataAt = System.currentTimeMillis()
            } else if (System.currentTimeMillis() - lastDataAt >= quietMs) {
                return
            }
        }
    }

    /* ── SLIP encoding ─────────────────────────────────────────────── */

    private fun slipEncode(data: ByteArray): ByteArray {
        val out = ArrayList<Byte>(data.size + 4)
        out.add(0xC0.toByte())
        for (b in data) {
            when (b) {
                0xC0.toByte() -> { out.add(0xDB.toByte()); out.add(0xDC.toByte()) }
                0xDB.toByte() -> { out.add(0xDB.toByte()); out.add(0xDD.toByte()) }
                else -> out.add(b)
            }
        }
        out.add(0xC0.toByte())
        return out.toByteArray()
    }

    private fun slipDecode(data: ByteArray): ByteArray {
        val out = ArrayList<Byte>(data.size)
        var i = 0
        while (i < data.size) {
            val b = data[i]
            if (b == 0xDB.toByte() && i + 1 < data.size) {
                when (data[i + 1]) {
                    0xDC.toByte() -> { out.add(0xC0.toByte()); i += 2; continue }
                    0xDD.toByte() -> { out.add(0xDB.toByte()); i += 2; continue }
                }
            }
            out.add(b); i++
        }
        return out.toByteArray()
    }

    /* ── Misc helpers ──────────────────────────────────────────────── */

    private fun packLeInts(vararg ints: Int): ByteArray {
        val out = ByteArray(ints.size * 4)
        for ((i, v) in ints.withIndex()) {
            out[i * 4]     = (v and 0xFF).toByte()
            out[i * 4 + 1] = ((v shr 8) and 0xFF).toByte()
            out[i * 4 + 2] = ((v shr 16) and 0xFF).toByte()
            out[i * 4 + 3] = ((v shr 24) and 0xFF).toByte()
        }
        return out
    }

    private fun readLeInt(buf: ByteArray, off: Int): Int =
        (buf[off].toInt() and 0xFF) or
        ((buf[off + 1].toInt() and 0xFF) shl 8) or
        ((buf[off + 2].toInt() and 0xFF) shl 16) or
        ((buf[off + 3].toInt() and 0xFF) shl 24)

    private data class Frame(val cmd: Byte, val size: Int, val value: Int, val payload: ByteArray)

    companion object {
        const val FLASH_BLOCK_SIZE = 0x4000   // 16 KB

        const val CMD_FLASH_BEGIN: Byte = 0x02
        const val CMD_FLASH_DATA: Byte  = 0x03
        const val CMD_FLASH_END: Byte   = 0x04
        const val CMD_SYNC: Byte        = 0x08
        const val CMD_READ_REG: Byte    = 0x0A

        private const val TAG = "EsptoolClient"
    }
}
