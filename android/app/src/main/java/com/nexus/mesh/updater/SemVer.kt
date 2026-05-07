package com.nexus.mesh.updater

/**
 * Tiny semver parser. Just enough for the updater — we only use this to
 * compare BuildConfig.VERSION_NAME against the latest GitHub release tag.
 *
 * Accepts: "0.6.2", "v0.6.2", "0.6.2-rc1" (the prerelease suffix is
 * compared lexicographically, with no-suffix counted as newer per
 * semver rules).
 */
data class SemVer(
    val major: Int,
    val minor: Int,
    val patch: Int,
    val pre: String? = null,
) : Comparable<SemVer> {

    override fun compareTo(other: SemVer): Int {
        if (major != other.major) return major.compareTo(other.major)
        if (minor != other.minor) return minor.compareTo(other.minor)
        if (patch != other.patch) return patch.compareTo(other.patch)
        // Per semver: a version WITHOUT a prerelease is considered
        // greater than one WITH a prerelease.
        return when {
            pre == null && other.pre == null -> 0
            pre == null -> 1
            other.pre == null -> -1
            else -> pre.compareTo(other.pre)
        }
    }

    override fun toString(): String =
        if (pre != null) "$major.$minor.$patch-$pre" else "$major.$minor.$patch"

    companion object {
        fun parse(raw: String): SemVer? {
            val s = raw.trim().removePrefix("v")
            val (head, pre) = if ('-' in s) s.split('-', limit = 2).let { it[0] to it[1] } else s to null
            val parts = head.split('.')
            if (parts.size < 3) return null
            return try {
                SemVer(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), pre)
            } catch (_: NumberFormatException) {
                null
            }
        }
    }
}
