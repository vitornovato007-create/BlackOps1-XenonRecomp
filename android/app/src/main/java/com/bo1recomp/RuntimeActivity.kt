package com.bo1recomp

import android.app.Activity
import android.graphics.Color
import android.graphics.Typeface
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import java.io.File
import java.io.RandomAccessFile

/** Tela de teste de inicializacao: roda o codigo recompilado e mostra o log do runtime. */
class RuntimeActivity : Activity() {

    private val ui = Handler(Looper.getMainLooper())

    private lateinit var statusView: TextView
    private lateinit var logView: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var startButton: Button

    private var gameDir = ""
    private var logPath = ""
    private var lastLog = ""

    private val refresh = object : Runnable {
        override fun run() {
            updateScreen()
            ui.postDelayed(this, 800)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        gameDir = intent.getStringExtra("game_dir") ?: ""
        logPath = File(getExternalFilesDir(null) ?: filesDir, "runtime.log").absolutePath

        val root = LinearLayout(this)
        root.orientation = LinearLayout.VERTICAL
        root.setBackgroundColor(Color.parseColor("#0D1014"))
        root.setPadding(32, 48, 32, 32)

        val title = TextView(this)
        title.text = "Black Ops 1 · teste de inicialização"
        title.setTextColor(Color.WHITE)
        title.textSize = 20f
        title.setTypeface(null, Typeface.BOLD)
        root.addView(title)

        val info = TextView(this)
        info.text = "Carrega o executável e roda o código recompilado. " +
            "Ainda sem vídeo, áudio, controles nem threads. " +
            "O objetivo é ver até onde o jogo chega e o que ele pede ao sistema."
        info.setTextColor(Color.parseColor("#9AA5B1"))
        info.textSize = 13f
        info.setPadding(0, 12, 0, 12)
        root.addView(info)

        statusView = TextView(this)
        statusView.setTextColor(Color.parseColor("#E8A33D"))
        statusView.textSize = 14f
        statusView.setPadding(0, 0, 0, 12)
        root.addView(statusView)

        val buttons = LinearLayout(this)
        buttons.orientation = LinearLayout.HORIZONTAL

        startButton = Button(this)
        startButton.text = "Iniciar teste"
        startButton.isAllCaps = false
        startButton.setOnClickListener { startRuntime() }
        buttons.addView(
            startButton,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        )

        val stopButton = Button(this)
        stopButton.text = "Parar"
        stopButton.isAllCaps = false
        stopButton.setOnClickListener { stopRuntime() }
        buttons.addView(
            stopButton,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        )

        val back = Button(this)
        back.text = "Voltar"
        back.isAllCaps = false
        back.setOnClickListener { finish() }
        buttons.addView(
            back,
            LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
        )

        root.addView(buttons)

        logView = TextView(this)
        logView.setTextColor(Color.parseColor("#C9D1D9"))
        logView.typeface = Typeface.MONOSPACE
        logView.textSize = 11f
        logView.setTextIsSelectable(true)
        logView.setPadding(0, 16, 0, 16)

        logScroll = ScrollView(this)
        logScroll.addView(logView)
        root.addView(
            logScroll,
            LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f)
        )

        setContentView(root)
    }

    override fun onResume() {
        super.onResume()
        ui.post(refresh)
    }

    override fun onPause() {
        super.onPause()
        ui.removeCallbacks(refresh)
    }

    private fun startRuntime() {
        if (!NativeRuntime.load()) {
            statusView.text = "A biblioteca nativa não carregou (libbo1runtime.so)."
            return
        }

        try {
            val message = NativeRuntime.nativeStart(gameDir, logPath)
            statusView.text = message
            startButton.isEnabled = false
        } catch (e: Throwable) {
            statusView.text = "Erro ao chamar o runtime: ${e.message}"
        }
    }

    private fun stopRuntime() {
        if (NativeRuntime.load()) {
            try {
                NativeRuntime.nativeStop()
            } catch (e: Throwable) {
                statusView.text = "Erro ao parar: ${e.message}"
            }
        }
    }

    private fun updateScreen() {
        if (NativeRuntime.load()) {
            try {
                statusView.text = "Estado: " + NativeRuntime.nativeState()
            } catch (e: Throwable) {
                statusView.text = "Estado indisponível: ${e.message}"
            }
        } else {
            statusView.text = "A biblioteca nativa não carregou (libbo1runtime.so)."
        }

        val text = readTail(logPath, 24 * 1024)

        if (text != lastLog) {
            lastLog = text
            logView.text = text
            logScroll.post { logScroll.fullScroll(View.FOCUS_DOWN) }
        }
    }

    private fun readTail(path: String, maxBytes: Int): String {
        val file = File(path)

        if (!file.isFile) {
            return "(ainda sem log)"
        }

        return try {
            RandomAccessFile(file, "r").use { raf ->
                val length = raf.length()
                val start = if (length > maxBytes) length - maxBytes else 0L
                raf.seek(start)
                val bytes = ByteArray((length - start).toInt())
                raf.readFully(bytes)
                String(bytes, Charsets.UTF_8)
            }
        } catch (e: Exception) {
            "(não consegui ler o log: ${e.message})"
        }
    }
}
