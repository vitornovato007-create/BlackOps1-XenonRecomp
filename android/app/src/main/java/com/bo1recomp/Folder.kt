package com.bo1recomp

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import java.io.File
import java.io.FileOutputStream
import java.io.IOException
import java.security.MessageDigest

/** Um arquivo dentro da pasta escolhida: caminho relativo, endereco (URI) e tamanho. */
class FolderFile(val path: String, val uri: Uri, val size: Long)

/** Percorre uma pasta escolhida pelo seletor do Android (acesso por URI de arvore). */
object FolderScanner {

    fun scan(resolver: ContentResolver, treeUri: Uri): List<FolderFile> {
        val out = ArrayList<FolderFile>()
        walk(resolver, treeUri, DocumentsContract.getTreeDocumentId(treeUri), "", out, 0)
        return out
    }

    private fun walk(
        resolver: ContentResolver,
        treeUri: Uri,
        parentId: String,
        prefix: String,
        out: MutableList<FolderFile>,
        depth: Int
    ) {
        if (depth > 32 || out.size > 200000) {
            return
        }

        val children = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, parentId)

        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE
        )

        val folders = ArrayList<Pair<String, String>>()

        resolver.query(children, columns, null, null, null)?.use { cursor ->
            while (cursor.moveToNext()) {
                val docId = cursor.getString(0) ?: continue
                val name = cursor.getString(1) ?: continue
                val mime = cursor.getString(2) ?: ""
                val size = if (cursor.isNull(3)) 0L else cursor.getLong(3)

                if (mime == DocumentsContract.Document.MIME_TYPE_DIR) {
                    folders.add(Pair(docId, name))
                } else {
                    val uri = DocumentsContract.buildDocumentUriUsingTree(treeUri, docId)
                    out.add(FolderFile("$prefix$name", uri, size))
                }
            }
        }

        for ((docId, name) in folders) {
            walk(resolver, treeUri, docId, "$prefix$name/", out, depth + 1)
        }
    }
}

/** Copia os arquivos de uma pasta ja extraida para a pasta do aplicativo. */
class FolderImporter(private val resolver: ContentResolver) {

    @Volatile
    var cancelled: Boolean = false

    fun importAll(
        files: List<FolderFile>,
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

            val target = safeChild(destRoot, file.path) ?: continue
            target.parentFile?.mkdirs()

            val isXex = file.path.equals("default.xex", ignoreCase = true)
            val digest = if (isXex) MessageDigest.getInstance("SHA-256") else null

            val input = resolver.openInputStream(file.uri)
                ?: throw IOException("Não consegui ler ${file.path}")

            input.use { stream ->
                FileOutputStream(target).use { out ->
                    while (true) {
                        if (cancelled) {
                            throw ImportCancelled()
                        }

                        val n = stream.read(buffer)

                        if (n < 0) {
                            break
                        }

                        if (n == 0) {
                            continue
                        }

                        out.write(buffer, 0, n)
                        digest?.update(buffer, 0, n)
                        done += n

                        val now = System.nanoTime()

                        if (now - lastReport > 200_000_000L) {
                            lastReport = now
                            onProgress(done, if (total > done) total else done, file.path)
                        }
                    }
                }
            }

            if (isXex && digest != null) {
                xexSize = target.length()
                xexHash = digest.digest().joinToString("") { "%02x".format(it) }
            }
        }

        onProgress(done, if (total > done) total else done, "")
        return ImportResult(files.size, done, xexSize, xexHash)
    }
}

/** Evita caminhos perigosos (.., barras invertidas, etc.) ao montar o destino de uma copia. */
fun safeChild(root: File, path: String): File? {
    var result = root

    for (part in path.split('/')) {
        if (part.isEmpty() || part == "." || part == ".." || part.contains('\\') || part.contains(':')) {
            return null
        }

        result = File(result, part)
    }

    return result
}
