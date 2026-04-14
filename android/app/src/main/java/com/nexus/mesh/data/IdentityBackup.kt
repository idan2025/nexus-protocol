package com.nexus.mesh.data

import android.util.Base64
import java.nio.ByteBuffer
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.GCMParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec

/**
 * Passphrase-encrypted identity backup.
 *
 * Wire format (versioned so we can rotate KDF later):
 *   [magic(4)="NXID"][version(1)=0x01][pbkdf2_iters(4 BE)][salt(16)][iv(12)][ct||tag(16)]
 *
 * KDF:  PBKDF2-HMAC-SHA256, 600_000 iters by default (OWASP 2023)
 * Enc:  AES-256-GCM, 96-bit IV, 128-bit tag
 *
 * Encoded as Base64 (no-wrap) for sharing via QR/paste/email.
 */
object IdentityBackup {

    private const val MAGIC = "NXID"
    private const val VERSION: Byte = 0x01
    private const val KDF_ITERATIONS = 600_000
    private const val SALT_LEN = 16
    private const val IV_LEN = 12
    private const val TAG_BITS = 128
    private const val KEY_BITS = 256

    class BadBackupException(msg: String) : Exception(msg)
    class BadPassphraseException : Exception("Wrong passphrase or corrupted backup")

    fun encrypt(identityBytes: ByteArray, passphrase: CharArray): String {
        require(identityBytes.isNotEmpty()) { "identity bytes empty" }
        require(passphrase.isNotEmpty()) { "passphrase empty" }

        val rng = SecureRandom()
        val salt = ByteArray(SALT_LEN).also { rng.nextBytes(it) }
        val iv = ByteArray(IV_LEN).also { rng.nextBytes(it) }

        val key = deriveKey(passphrase, salt, KDF_ITERATIONS)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.ENCRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(TAG_BITS, iv))
        val ct = cipher.doFinal(identityBytes)

        val buf = ByteBuffer.allocate(4 + 1 + 4 + SALT_LEN + IV_LEN + ct.size)
        buf.put(MAGIC.toByteArray(Charsets.US_ASCII))
        buf.put(VERSION)
        buf.putInt(KDF_ITERATIONS)
        buf.put(salt)
        buf.put(iv)
        buf.put(ct)
        return Base64.encodeToString(buf.array(), Base64.NO_WRAP)
    }

    fun decrypt(blob: String, passphrase: CharArray): ByteArray {
        val raw = try {
            Base64.decode(blob.trim(), Base64.DEFAULT)
        } catch (e: IllegalArgumentException) {
            throw BadBackupException("not valid base64")
        }
        if (raw.size < 4 + 1 + 4 + SALT_LEN + IV_LEN + 16) {
            throw BadBackupException("blob too short")
        }
        val buf = ByteBuffer.wrap(raw)
        val magic = ByteArray(4).also { buf.get(it) }
        if (!magic.contentEquals(MAGIC.toByteArray(Charsets.US_ASCII))) {
            throw BadBackupException("bad magic (not a NEXUS identity backup)")
        }
        val version = buf.get()
        if (version != VERSION) {
            throw BadBackupException("unsupported version $version")
        }
        val iters = buf.int
        if (iters !in 1_000..10_000_000) {
            throw BadBackupException("implausible iter count $iters")
        }
        val salt = ByteArray(SALT_LEN).also { buf.get(it) }
        val iv = ByteArray(IV_LEN).also { buf.get(it) }
        val ct = ByteArray(buf.remaining()).also { buf.get(it) }

        val key = deriveKey(passphrase, salt, iters)
        val cipher = Cipher.getInstance("AES/GCM/NoPadding")
        cipher.init(Cipher.DECRYPT_MODE, SecretKeySpec(key, "AES"), GCMParameterSpec(TAG_BITS, iv))
        return try {
            cipher.doFinal(ct)
        } catch (e: javax.crypto.AEADBadTagException) {
            throw BadPassphraseException()
        }
    }

    private fun deriveKey(passphrase: CharArray, salt: ByteArray, iters: Int): ByteArray {
        val spec = PBEKeySpec(passphrase, salt, iters, KEY_BITS)
        return try {
            SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).encoded
        } finally {
            spec.clearPassword()
        }
    }
}
