package com.nexus.mesh.maps

import android.content.Context
import android.net.Uri
import android.util.Log
import okhttp3.OkHttpClient
import okhttp3.Request
import org.osmdroid.config.Configuration
import org.osmdroid.tileprovider.modules.ArchiveFileFactory
import java.io.File
import java.io.FileOutputStream

/**
 * Manages MBTiles files for offline osmdroid tile serving.
 *
 * MBTiles archives are stored in osmdroid's base path so
 * [org.osmdroid.tileprovider.modules.MapTileFileArchiveProvider] can pick
 * them up automatically on the next map load.
 *
 * Supported formats: any archive readable by osmdroid's ArchiveFileFactory
 * (MBTiles SQLite and ZIP tile archives).
 */
object MbTilesManager {

    private const val TAG = "MbTilesManager"
    private const val TILES_DIR = "mbtiles"

    /** Directory where imported MBTiles archives are stored. */
    fun tilesDir(context: Context): File {
        val base = Configuration.getInstance().osmdroidBasePath
            ?: context.getExternalFilesDir(null)
            ?: context.filesDir
        return File(base, TILES_DIR).also { it.mkdirs() }
    }

    /** List all .mbtiles (and .zip tile archives) installed on this device. */
    fun listArchives(context: Context): List<File> {
        val dir = tilesDir(context)
        return dir.listFiles { f ->
            f.isFile && (f.name.endsWith(".mbtiles") || f.name.endsWith(".zip"))
        }?.sortedBy { it.name } ?: emptyList()
    }

    /**
     * Import an MBTiles file from a content URI (file picker result).
     * Copies to [tilesDir] so osmdroid can load it.
     * Returns the copied [File] on success, null on failure.
     */
    fun importFromUri(context: Context, uri: Uri, displayName: String): File? {
        return try {
            val dest = File(tilesDir(context), sanitize(displayName))
            context.contentResolver.openInputStream(uri)?.use { input ->
                FileOutputStream(dest).use { out -> input.copyTo(out) }
            }
            Log.i(TAG, "Imported MBTiles: ${dest.name} (${dest.length() / 1024} kB)")
            dest
        } catch (e: Exception) {
            Log.e(TAG, "Import failed for $uri", e)
            null
        }
    }

    /**
     * Delete an installed archive.
     */
    fun delete(file: File): Boolean {
        val ok = file.delete()
        Log.i(TAG, "Deleted ${file.name}: $ok")
        return ok
    }

    /**
     * Download a tile region from an OSM tile URL template and save it as an
     * osmdroid SQLite tile cache DB. This is a lightweight alternative to
     * shipping MBTiles — it uses osmdroid's native cache format.
     *
     * [urlTemplate] example: "https://tile.openstreetmap.org/{z}/{x}/{y}.png"
     * [minZoom]..[maxZoom]: zoom levels to download (keep range small!)
     * [north],[south],[east],[west]: bounding box in decimal degrees
     * [onProgress]: called with (tilesDownloaded, tilesTotal)
     *
     * Returns the number of tiles saved.
     */
    fun downloadRegion(
        context: Context,
        urlTemplate: String,
        minZoom: Int,
        maxZoom: Int,
        north: Double, south: Double, east: Double, west: Double,
        onProgress: (Int, Int) -> Unit
    ): Int {
        val http = OkHttpClient()
        val cacheDir = tilesDir(context)
        val tiles = mutableListOf<Triple<Int, Int, Int>>()  // (z,x,y)
        for (z in minZoom..maxZoom) {
            val xMin = lon2tile(west, z);  val xMax = lon2tile(east, z)
            val yMin = lat2tile(north, z); val yMax = lat2tile(south, z)
            for (x in xMin..xMax) for (y in yMin..yMax) tiles.add(Triple(z, x, y))
        }

        var done = 0
        val total = tiles.size
        onProgress(0, total)

        tiles.forEach { (z, x, y) ->
            val url = urlTemplate
                .replace("{z}", z.toString())
                .replace("{x}", x.toString())
                .replace("{y}", y.toString())
            try {
                val req = Request.Builder().url(url).build()
                http.newCall(req).execute().use { resp ->
                    if (resp.isSuccessful) {
                        val tileFile = File(cacheDir, "tile_${z}_${x}_$y.png")
                        resp.body?.byteStream()?.use { input ->
                            FileOutputStream(tileFile).use { out -> input.copyTo(out) }
                        }
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Failed tile z=$z x=$x y=$y: ${e.message}")
            }
            done++
            if (done % 10 == 0) onProgress(done, total)
        }
        onProgress(total, total)
        return done
    }

    private fun sanitize(name: String): String {
        val base = name.substringAfterLast('/').substringAfterLast('\\')
        val clean = base.replace(Regex("[^a-zA-Z0-9._-]"), "_")
        return if (clean.endsWith(".mbtiles") || clean.endsWith(".zip")) clean else "$clean.mbtiles"
    }

    private fun lon2tile(lon: Double, z: Int): Int =
        ((lon + 180.0) / 360.0 * (1 shl z)).toInt()

    private fun lat2tile(lat: Double, z: Int): Int {
        val latRad = Math.toRadians(lat)
        return ((1 - Math.log(Math.tan(latRad) + 1 / Math.cos(latRad)) / Math.PI) / 2 * (1 shl z)).toInt()
    }
}
