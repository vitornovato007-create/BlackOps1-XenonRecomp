package com.bo1recomp

/** Ponte com a biblioteca nativa (libbo1runtime.so): o runtime do jogo, em C++. */
object NativeRuntime {

    private var loaded = false

    fun load(): Boolean {
        if (loaded) {
            return true
        }

        return try {
            System.loadLibrary("bo1runtime")
            loaded = true
            true
        } catch (e: UnsatisfiedLinkError) {
            false
        }
    }

    @JvmStatic
    external fun nativeStart(gameDir: String, logPath: String): String

    @JvmStatic
    external fun nativeState(): String

    @JvmStatic
    external fun nativeStop()
}
