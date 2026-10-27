package com.bo1recomp

import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.nio.ByteBuffer
import java.nio.channels.FileChannel
import java.security.MessageDigest

/** Um arquivo dentro da ISO: caminho, setor inicial (relativo a particao) e tamanho. */
class IsoFile(val path: String, val sector: Long, val size: Long)

/** Leitor do sistema de arquivos dos discos do Xbox 360 (XDVDFS / XGD). */
class Xdvdfs(private val channel: FileChannel) {

    var partitionOffset: Long = 0L
        private set

    private var rootSector: Long = 0L
    private var rootSize: Long = 0L

    companion object {
        const val SECTOR = 2048L

        private val MAGIC = "MICROSOFT*XBOX*MEDIA".toByteArray(Charsets.US_ASCII)
        private val OFFSETS = longArrayOf(0x0L, 0xFD90000L, 0x2080000L, 0x18300000L)

        private fun u16(a: ByteArray, o: Int): Int =
            (a[o].toInt() and 0xFF) or ((a[o + 1].toInt() and 0xFF) shl 8)

        private fun u32(a: ByteArray, o: Int): Long =
            (a[o].toLong() and 0xFF) or
                ((a[o + 1].toLong() and 0xFF) shl 8) or
                ((a[o + 2].toLong() and 0xFF) shl 16) or
                ((a[o + 3].toLong() and 0xFF) shl 24)
    }

    /** Le [length] bytes a partir de [position]; devolve null se a ISO acabar antes. */
    fun readBytes(position: Long, length: Int): ByteArray? {
        val array = ByteArray(length)
        val buffer = ByteBuffer.wrap(array)
        var total = 0

        while (total < length) {
            val n = channel.read(buffer, position + total)

            if (n < 0) {
                return null
            }

            total += n
        }

        return array
    }

    /** Procura o descritor de volume nos deslocamentos conhecidos (XGD1, XGD2, XGD3). */
    fun open(): Boolean {
        for (base in OFFSETS) {
            val header = readBytes(base + 32L * SECTOR, 28) ?: continue

            var match = true

            for (i in MAGIC.indices) {
                if (header[i] != MAGIC[i]) {
                    match = false
                    break
                }
            }

            if (!match) {
                continue
            }

            partitionOffset = base
            rootSector = u32(header, 20)
            rootSize = u32(header, 24)
            return true
        }

        return false
    }

    fun listFiles(): List<IsoFile> {
        val out = ArrayList<IsoFile>()
        walkDirectory(rootSector, rootSize, "", out, 0)
        return out
    }

    private fun walkDirectory(sector: Long, size: Long, prefix: String, out: MutableList<IsoFile>, depth: Int) {
        if (depth > 32 || size <= 0L || size > 64L * 1024L * 1024L) {
            return
        }

        val dir = readBytes(partitionOffset + sector * SECTOR, size.toInt())
            ?: throw IOException("Diretório fora do arquivo ISO")

        val visited = HashSet<Int>()
        val stack = ArrayList<Int>()
        stack.add(0)

        while (stack.isNotEmpty()) {
            val offset = stack.removeAt(stack.size - 1)

            if (offset < 0 || offset + 14 > dir.size) {
                continue
            }

            if (!visited.add(offset)) {
                continue
            }

            val left = u16(dir, offset)
            val right = u16(dir, offset + 2)
            val start = u32(dir, offset + 4)
            val length = u32(dir, offset + 8)
            val attr = dir[offset + 12].toInt() and 0xFF
            val nameLen = dir[offset + 13].toInt() and 0xFF

            if (start == 0xFFFFFFFFL || nameLen == 0 || offset + 14 + nameLen > dir.size) {
                continue
            }

            val name = String(dir, offset + 14, nameLen, Charsets.ISO_8859_1)

            if (left != 0 && left != 0xFFFF) {
                stack.add(left * 4)
            }

            if (right != 0 && right != 0xFFFF) {
                stack.add(right * 4)
            }

            if ((attr and 0x10) != 0) {
                walkDirectory(start, length, "$prefix$name/", out, depth + 1)
            } else {
                out.add(IsoFile("$prefix$name", start, length))
            }
        }
    }
}

class ImportResult(val files: Int, val bytes: Long, val xexSize: Long, val xexSha256: String)

class ImportCancelled : Exception("Importação cancelada")

/** Copia os arquivos da ISO para uma pasta do aplicativo, calculando o SHA-256 do default.xex. */
class Importer(private val channel: FileChannel, private val iso: Xdvdfs) {

    @Volatile
    var cancelled: Boolean = false

    fun importAll(
        files: List<IsoFile>,
        destRoot: File,
        onProgress: (done: Long, total: Long, current: String) -> Unit
    ): ImportResult {
        val total = files.fold(0L) { acc, f -> acc + f.size }
        var done = 0L
        var xexSize = 0L
        var xexHash = ""
        var lastReport = 0L
        val buffer = ByteArray(4 * 1024 * 1024)

        for (file in files) {
            if (cancelled) {
                throw ImportCancelled()
            }

            val target = safeTarget(destRoot, file.path) ?: continue
            target.parentFile?.mkdirs()

            val isXex = file.path.equals("default.xex", ignoreCase = true)
            val digest = if (isXex) MessageDigest.getInstance("SHA-256") else null

            FileOutputStream(target).use { out ->
                var position = iso.partitionOffset + file.sector * Xdvdfs.SECTOR
                var remaining = file.size

                while (remaining > 0L) {
                    if (cancelled) {
                        throw ImportCancelled()
                    }

                    val want = if (remaining < buffer.size.toLong()) remaining.toInt() else buffer.size
                    val byteBuffer = ByteBuffer.wrap(buffer, 0, want)
                    var got = 0

                    while (got < want) {
                        val n = channel.read(byteBuffer, position + got)

                        if (n < 0) {
                            throw IOException("Fim inesperado da ISO em ${file.path}")
                        }

                        got += n
                    }

                    out.write(buffer, 0, want)
                    digest?.update(buffer, 0, want)

                    position += want
                    remaining -= want
                    done += want

                    val now = System.nanoTime()

                    if (now - lastReport > 200_000_000L) {
                        lastReport = now
                        onProgress(done, total, file.path)
                    }
                }
            }

            if (isXex && digest != null) {
                xexSize = file.size
                xexHash = digest.digest().joinToString("") { "%02x".format(it) }
            }
        }

        onProgress(done, total, "")
        return ImportResult(files.size, done, xexSize, xexHash)
    }

    /** Evita caminhos perigosos vindos da ISO (.., barras invertidas, etc.). */
    private fun safeTarget(root: File, path: String): File? {
        var result = root

        for (part in path.split('/')) {
            if (part.isEmpty() || part == "." || part == ".." || part.contains('\\') || part.contains(':')) {
                return null
            }

            result = File(result, part)
        }

        return result
    }
}
