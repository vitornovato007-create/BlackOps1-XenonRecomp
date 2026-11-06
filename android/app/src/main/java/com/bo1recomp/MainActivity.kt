package com.bo1recomp

import android.app.Activity
import android.app.AlertDialog
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Color
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.os.StatFs
import android.view.View
import android.view.ViewGroup
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ProgressBar
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.io.IOException
import java.nio.channels.FileChannel
import java.util.Locale

/** Ponto onde o runtime do jogo sera ligado nas proximas etapas. */
object RuntimeBridge {
    fun start(gameDir: String): String =
        "O runtime do jogo ainda não está integrado a este aplicativo.\n\n" +
            "Próximas etapas: ligar o código recompilado (ARM64) e o runtime " +
            "(kernel do Xbox 360, GPU, áudio e entrada).\n\n" +
            "Pasta do jogo:\n$gameDir"
}

fun formatBytes(bytes: Long): String =
    String.format(Locale.US, "%.2f GB", bytes / 1073741824.0)

class MainActivity : Activity() {

    companion object {
        const val REQUEST_PICK_ISO = 1001
        const val REQUEST_PICK_FOLDER = 1002
        const val EXPECTED_XEX_SIZE = 8310784L
        const val PREFS = "bo1_launcher"
        const val MARGIN_BYTES = 300L * 1024L * 1024L
    }

    private val ui = Handler(Looper.getMainLooper())
    private lateinit var prefs: SharedPreferences

    private lateinit var statusText: TextView
    private lateinit var detailText: TextView
    private lateinit var progressBar: ProgressBar
    private lateinit var pickButton: Button
    private lateinit var pickFolderButton: Button
    private lateinit var importButton: Button
    private lateinit var cancelButton: Button
    private lateinit var startButton: Button
    private lateinit var deleteButton: Button

    private var pickedUri: Uri? = null
    private var pickedFiles: List<IsoFile> = emptyList()
    private var pickedFolderUri: Uri? = null
    private var pickedFolderFiles: List<FolderFile> = emptyList()
    private var folderMode = false
    private var importing = false
    private var canImport = false
    private var ready = false
    private var busy = false
    private var importer: Importer? = null
    private var folderImporter: FolderImporter? = null

    private val colorBackground = Color.parseColor("#12161C")
    private val colorCard = Color.parseColor("#1B2129")
    private val colorAccent = Color.parseColor("#E8A33D")
    private val colorSecondary = Color.parseColor("#2F3A47")
    private val colorDanger = Color.parseColor("#B83C3C")
    private val colorTextDim = Color.parseColor("#9AA5B1")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE)

        buildUi()
        refreshFromDisk()
    }

    // ------------------------------------------------------------------ interface

    private fun dp(value: Int): Int = (value * resources.displayMetrics.density + 0.5f).toInt()

    private fun makeButton(label: String, color: Int): Button {
        val button = Button(this)
        button.text = label
        button.isAllCaps = false
        button.setTextColor(Color.WHITE)
        button.textSize = 16f
        button.setPadding(dp(16), dp(14), dp(16), dp(14))

        val shape = GradientDrawable()
        shape.cornerRadius = dp(12).toFloat()
        shape.setColor(color)
        button.background = shape

        val params = LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT,
            ViewGroup.LayoutParams.WRAP_CONTENT
        )
        params.topMargin = dp(10)
        button.layoutParams = params

        return button
    }

    private fun buildUi() {
        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setPadding(dp(20), dp(28), dp(20), dp(28))

        val title = TextView(this)
        title.text = "Call of Duty: Black Ops"
        title.setTextColor(Color.WHITE)
        title.textSize = 26f
        title.setTypeface(null, Typeface.BOLD)
        root.addView(title)

        val subtitle = TextView(this)
        subtitle.text = "Recompilação para Android (ARM64) · instalação"
        subtitle.setTextColor(colorAccent)
        subtitle.textSize = 14f
        subtitle.setPadding(0, dp(4), 0, dp(20))
        root.addView(subtitle)

        val card = LinearLayout(this)
        card.orientation = LinearLayout.VERTICAL
        card.setPadding(dp(18), dp(18), dp(18), dp(18))

        val cardShape = GradientDrawable()
        cardShape.cornerRadius = dp(16).toFloat()
        cardShape.setColor(colorCard)
        card.background = cardShape

        statusText = TextView(this)
        statusText.setTextColor(Color.WHITE)
        statusText.textSize = 18f
        statusText.setTypeface(null, Typeface.BOLD)
        card.addView(statusText)

        detailText = TextView(this)
        detailText.setTextColor(colorTextDim)
        detailText.textSize = 14f
        detailText.setPadding(0, dp(8), 0, dp(8))
        card.addView(detailText)

        progressBar = ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal)
        progressBar.max = 1000
        progressBar.visibility = View.GONE
        card.addView(progressBar)

        pickButton = makeButton("Selecionar ISO do jogo", colorAccent)
        pickButton.setTextColor(Color.BLACK)
        pickButton.setOnClickListener { pickIso() }
        card.addView(pickButton)

        pickFolderButton = makeButton("Selecionar pasta do jogo (já extraída)", colorSecondary)
        pickFolderButton.setOnClickListener { pickFolder() }
        card.addView(pickFolderButton)

        importButton = makeButton("Importar para o aplicativo", colorSecondary)
        importButton.setOnClickListener { startImport() }
        card.addView(importButton)

        cancelButton = makeButton("Cancelar importação", colorDanger)
        cancelButton.setOnClickListener {
            importer?.cancelled = true
            folderImporter?.cancelled = true
        }
        card.addView(cancelButton)

        startButton = makeButton("Iniciar jogo", colorAccent)
        startButton.setTextColor(Color.BLACK)
        startButton.setOnClickListener { startGame() }
        card.addView(startButton)

        deleteButton = makeButton("Apagar dados importados", colorSecondary)
        deleteButton.setOnClickListener { confirmDelete() }
        card.addView(deleteButton)

        root.addView(card)

        val note = TextView(this)
        note.text = "Os arquivos do jogo não acompanham o aplicativo. " +
            "Use a ISO do seu próprio disco do Black Ops 1 (Xbox 360). " +
            "Mantenha a tela ligada durante a importação."
        note.setTextColor(colorTextDim)
        note.textSize = 12f
        note.setPadding(0, dp(16), 0, 0)
        root.addView(note)

        val scroll = ScrollView(this)
        scroll.setBackgroundColor(colorBackground)
        scroll.addView(root)
        setContentView(scroll)
    }

    private fun setEnabled(button: Button, enabled: Boolean, visible: Boolean = true) {
        button.isEnabled = enabled
        button.alpha = if (enabled) 1f else 0.4f
        button.visibility = if (visible) View.VISIBLE else View.GONE
    }

    private fun refreshButtons() {
        setEnabled(pickButton, !busy)
        setEnabled(pickFolderButton, !busy)
        setEnabled(importButton, !busy && canImport, visible = canImport)
        setEnabled(cancelButton, busy && importing, visible = busy && importing)
        setEnabled(startButton, !busy && ready)
        setEnabled(deleteButton, !busy && ready, visible = ready)
        progressBar.visibility = if (busy && importing) View.VISIBLE else View.GONE
    }

    private fun show(status: String, detail: String, error: Boolean = false) {
        statusText.text = status
        statusText.setTextColor(if (error) Color.parseColor("#FF6B6B") else Color.WHITE)
        detailText.text = detail
    }

    // ------------------------------------------------------------------ estado em disco

    private fun gameBaseDir(): File = getExternalFilesDir(null) ?: filesDir

    private fun gameDir(): File = File(gameBaseDir(), "game")

    private fun refreshFromDisk() {
        val xex = File(gameDir(), "default.xex")
        val expected = prefs.getLong("xex_size", -1L)

        ready = prefs.getBoolean("ready", false) && xex.isFile && xex.length() == expected

        if (ready) {
            show(
                "3/3 · Jogo pronto",
                "Importado: ${prefs.getLong("files", 0L)} arquivos, " +
                    "${formatBytes(prefs.getLong("bytes", 0L))}.\n" +
                    "default.xex: ${xex.length()} bytes\n" +
                    "SHA-256: ${prefs.getString("xex_sha256", "")}"
            )
        } else {
            show(
                "1/3 · Selecione a ISO do jogo",
                "Escolha o arquivo .iso do disco do Black Ops 1 (Xbox 360) ou uma pasta " +
                    "com os arquivos já extraídos (precisa conter o default.xex). " +
                    "Prefira arquivos no armazenamento do próprio aparelho."
            )
        }

        refreshButtons()
    }

    // ------------------------------------------------------------------ escolher e analisar a ISO

    private fun pickIso() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT)
        intent.addCategory(Intent.CATEGORY_OPENABLE)
        intent.type = "*/*"
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        startActivityForResult(intent, REQUEST_PICK_ISO)
    }

    private fun pickFolder() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION)
        startActivityForResult(intent, REQUEST_PICK_FOLDER)
    }

    @Deprecated("Usa a API classica de Activity para manter o projeto simples")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)

        if (resultCode != RESULT_OK) {
            return
        }

        val uri = data?.data ?: return

        if (requestCode == REQUEST_PICK_FOLDER) {
            try {
                contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
            } catch (e: Exception) {
                // sem permissao persistente; a leitura agora continua valendo
            }

            analyzeFolder(uri)
            return
        }

        if (requestCode != REQUEST_PICK_ISO) {
            return
        }

        try {
            contentResolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION)
        } catch (e: Exception) {
            // nem todo provedor permite permissao persistente; a leitura agora continua valendo
        }

        analyze(uri)
    }

    private fun openChannel(uri: Uri): FileChannel {
        val pfd = contentResolver.openFileDescriptor(uri, "r")
            ?: throw IOException("Não consegui abrir o arquivo escolhido")

        return ParcelFileDescriptor.AutoCloseInputStream(pfd).channel
    }

    private fun analyze(uri: Uri) {
        busy = true
        importing = false
        folderMode = false
        canImport = false
        show("2/3 · Analisando a ISO…", "Lendo o sistema de arquivos do disco. Isso leva alguns segundos.")
        refreshButtons()

        Thread {
            try {
                openChannel(uri).use { channel ->
                    val xd = Xdvdfs(channel)

                    if (!xd.open()) {
                        throw IOException("Isso não parece uma ISO de Xbox 360 (formato XGD).")
                    }

                    val files = xd.listFiles()
                    val xex = files.firstOrNull { it.path.equals("default.xex", ignoreCase = true) }
                        ?: throw IOException("default.xex não encontrado na raiz do disco.")

                    val magic = xd.readBytes(xd.partitionOffset + xex.sector * Xdvdfs.SECTOR, 4)

                    if (magic == null || String(magic, Charsets.US_ASCII) != "XEX2") {
                        throw IOException("O default.xex do disco não tem o formato esperado (XEX2).")
                    }

                    val total = files.fold(0L) { acc, f -> acc + f.size }
                    ui.post { onAnalyzed(uri, files, total, xex.size) }
                }
            } catch (e: Exception) {
                ui.post { onError("Falha ao analisar: ${e.message}") }
            }
        }.start()
    }

    private fun onAnalyzed(uri: Uri, files: List<IsoFile>, total: Long, xexSize: Long) {
        pickedUri = uri
        pickedFiles = files
        busy = false

        val free = StatFs(gameBaseDir().absolutePath).availableBytes
        val enough = free >= total + MARGIN_BYTES

        val version = if (xexSize == EXPECTED_XEX_SIZE) {
            "versão reconhecida"
        } else {
            "ATENÇÃO: tamanho diferente do esperado ($EXPECTED_XEX_SIZE bytes); o código recompilado pode não servir"
        }

        canImport = enough

        show(
            if (enough) "2/3 · ISO válida" else "2/3 · Espaço insuficiente",
            "${files.size} arquivos · ${formatBytes(total)}\n" +
                "default.xex: $xexSize bytes ($version)\n" +
                "Espaço livre: ${formatBytes(free)}" +
                if (enough) "" else "\nLibere pelo menos ${formatBytes(total + MARGIN_BYTES - free)} e tente de novo.",
            error = !enough
        )

        refreshButtons()
    }

    private fun onError(message: String) {
        busy = false
        importing = false
        importer = null
        folderImporter = null
        show("Algo deu errado", message, error = true)
        refreshButtons()
    }

    // ------------------------------------------------------------------ importar

    private fun startImport() {
        if (folderMode) {
            startFolderImport()
        } else {
            startIsoImport()
        }
    }

    private fun startIsoImport() {
        val uri = pickedUri ?: return
        val files = pickedFiles

        val base = gameBaseDir()
        val tmp = File(base, "game_tmp")
        val finalDir = gameDir()

        busy = true
        importing = true
        progressBar.progress = 0
        show("Importando…", "Copiando os arquivos do jogo. Não feche o aplicativo.")

        refreshButtons()

        Thread {
            try {
                tmp.deleteRecursively()
                tmp.mkdirs()

                openChannel(uri).use { channel ->
                    val xd = Xdvdfs(channel)

                    if (!xd.open()) {
                        throw IOException("ISO inválida")
                    }

                    val worker = Importer(channel, xd)
                    importer = worker
                    ui.post { refreshButtons() }

                    val result = worker.importAll(files, tmp) { done, total, current ->
                        ui.post { onProgress(done, total, current) }
                    }

                    finalDir.deleteRecursively()

                    if (!tmp.renameTo(finalDir)) {
                        throw IOException("Não consegui finalizar a pasta do jogo")
                    }

                    prefs.edit()
                        .putBoolean("ready", true)
                        .putLong("files", result.files.toLong())
                        .putLong("bytes", result.bytes)
                        .putLong("xex_size", result.xexSize)
                        .putString("xex_sha256", result.xexSha256)
                        .apply()

                    ui.post { onImported() }
                }
            } catch (e: ImportCancelled) {
                tmp.deleteRecursively()
                ui.post { onError("Importação cancelada.") }
            } catch (e: Exception) {
                tmp.deleteRecursively()
                ui.post { onError("Falha na importação: ${e.message}") }
            }
        }.start()
    }

    private fun onProgress(done: Long, total: Long, current: String) {
        val percent = if (total > 0L) (done * 1000L / total).toInt().coerceAtMost(1000) else 0
        progressBar.progress = percent

        show(
            "Importando… ${percent / 10}%",
            "${formatBytes(done)} de ${formatBytes(total)}\n$current"
        )
    }

    private fun onImported() {
        busy = false
        importing = false
        importer = null
        folderImporter = null
        canImport = false
        pickedUri = null
        pickedFiles = emptyList()
        pickedFolderUri = null
        pickedFolderFiles = emptyList()
        refreshFromDisk()
    }

    // ------------------------------------------------------------------ pasta ja extraida

    private fun analyzeFolder(treeUri: Uri) {
        busy = true
        importing = false
        folderMode = true
        canImport = false
        show("2/3 · Analisando a pasta…", "Listando os arquivos. Isso pode levar alguns segundos.")
        refreshButtons()

        Thread {
            try {
                val all = FolderScanner.scan(contentResolver, treeUri)

                // acha o default.xex mais perto da raiz; se estiver numa subpasta, usa ela como raiz do jogo
                val candidates = all.filter {
                    it.path.substringAfterLast('/').equals("default.xex", ignoreCase = true)
                }

                val best = candidates.minByOrNull { it.path.count { c -> c == '/' } }
                    ?: throw IOException("default.xex não encontrado na pasta escolhida.")

                val prefix = if (best.path.contains('/')) best.path.substringBeforeLast('/') + "/" else ""

                val files = all
                    .filter { it.path.startsWith(prefix) }
                    .map { FolderFile(it.path.removePrefix(prefix), it.uri, it.size) }

                val xex = files.first { it.path.equals("default.xex", ignoreCase = true) }

                val head = ByteArray(4)
                var read = 0

                contentResolver.openInputStream(xex.uri)?.use { stream ->
                    while (read < 4) {
                        val n = stream.read(head, read, 4 - read)

                        if (n < 0) {
                            break
                        }

                        read += n
                    }
                }

                if (read < 4 || String(head, Charsets.US_ASCII) != "XEX2") {
                    throw IOException("O default.xex da pasta não tem o formato esperado (XEX2).")
                }

                val total = files.fold(0L) { acc, f -> acc + f.size }
                ui.post { onFolderAnalyzed(treeUri, files, total, xex.size, prefix) }
            } catch (e: Exception) {
                ui.post { onError("Falha ao analisar a pasta: ${e.message}") }
            }
        }.start()
    }

    private fun onFolderAnalyzed(treeUri: Uri, files: List<FolderFile>, total: Long, xexSize: Long, prefix: String) {
        pickedFolderUri = treeUri
        pickedFolderFiles = files
        busy = false

        val free = StatFs(gameBaseDir().absolutePath).availableBytes
        val enough = free >= total + MARGIN_BYTES

        val version = if (xexSize == EXPECTED_XEX_SIZE) {
            "versão reconhecida"
        } else {
            "ATENÇÃO: tamanho diferente do esperado ($EXPECTED_XEX_SIZE bytes); o código recompilado pode não servir"
        }

        val where = if (prefix.isEmpty()) "" else "\nO default.xex estava na subpasta \"${prefix.trimEnd('/')}\"; ela será usada como raiz do jogo."

        canImport = enough

        show(
            if (enough) "2/3 · Pasta válida" else "2/3 · Espaço insuficiente",
            "${files.size} arquivos · ${formatBytes(total)}\n" +
                "default.xex: $xexSize bytes ($version)\n" +
                "Espaço livre: ${formatBytes(free)}" +
                where +
                if (enough) "" else "\nLibere pelo menos ${formatBytes(total + MARGIN_BYTES - free)} e tente de novo.",
            error = !enough
        )

        refreshButtons()
    }

    private fun startFolderImport() {
        val files = pickedFolderFiles

        if (files.isEmpty()) {
            return
        }

        val base = gameBaseDir()
        val tmp = File(base, "game_tmp")
        val finalDir = gameDir()

        busy = true
        importing = true
        progressBar.progress = 0
        show("Importando…", "Copiando os arquivos do jogo. Não feche o aplicativo.")

        refreshButtons()

        Thread {
            try {
                tmp.deleteRecursively()
                tmp.mkdirs()

                val worker = FolderImporter(contentResolver)
                folderImporter = worker

                val result = worker.importAll(files, tmp) { done, total, current ->
                    ui.post { onProgress(done, total, current) }
                }

                finalDir.deleteRecursively()

                if (!tmp.renameTo(finalDir)) {
                    throw IOException("Não consegui finalizar a pasta do jogo")
                }

                prefs.edit()
                    .putBoolean("ready", true)
                    .putLong("files", result.files.toLong())
                    .putLong("bytes", result.bytes)
                    .putLong("xex_size", result.xexSize)
                    .putString("xex_sha256", result.xexSha256)
                    .apply()

                ui.post { onImported() }
            } catch (e: ImportCancelled) {
                tmp.deleteRecursively()
                ui.post { onError("Importação cancelada.") }
            } catch (e: Exception) {
                tmp.deleteRecursively()
                ui.post { onError("Falha na importação: ${e.message}") }
            }
        }.start()
    }

    // ------------------------------------------------------------------ iniciar e apagar

    private fun startGame() {
        val intent = Intent(this, RuntimeActivity::class.java)
        intent.putExtra("game_dir", gameDir().absolutePath)
        intent.putExtra("xex_sha256", prefs.getString("xex_sha256", ""))
        startActivity(intent)
    }

    private fun confirmDelete() {
        AlertDialog.Builder(this)
            .setMessage("Apagar os arquivos importados do jogo? Você poderá importar de novo depois.")
            .setPositiveButton("Apagar") { _, _ -> deleteImported() }
            .setNegativeButton("Cancelar", null)
            .show()
    }

    private fun deleteImported() {
        busy = true
        show("Apagando…", "Removendo os arquivos importados.")
        refreshButtons()

        Thread {
            gameDir().deleteRecursively()
            prefs.edit().clear().apply()

            ui.post {
                busy = false
                ready = false
                refreshFromDisk()
            }
        }.start()
    }
}

/** Tela do jogo. Por enquanto so mostra o estado: o runtime ainda sera integrado. */
class GameActivity : Activity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        val gameDir = intent.getStringExtra("game_dir") ?: ""
        val sha = intent.getStringExtra("xex_sha256") ?: ""

        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setBackgroundColor(Color.BLACK)
        root.setPadding(48, 64, 48, 48)

        val title = TextView(this)
        title.text = "Black Ops 1 · Recomp"
        title.setTextColor(Color.WHITE)
        title.textSize = 22f
        title.setTypeface(null, Typeface.BOLD)
        root.addView(title)

        val info = TextView(this)
        info.text = RuntimeBridge.start(gameDir) + "\n\nSHA-256 do default.xex:\n" + sha
        info.setTextColor(Color.LTGRAY)
        info.textSize = 14f
        info.setPadding(0, 32, 0, 32)
        root.addView(info)

        val back = Button(this)
        back.text = "Voltar"
        back.setOnClickListener { finish() }
        root.addView(back)

        val scroll = ScrollView(this)
        scroll.setBackgroundColor(Color.BLACK)
        scroll.addView(root)
        setContentView(scroll)
    }
}
