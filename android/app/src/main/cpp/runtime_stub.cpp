// Versao sem o codigo recompilado: so existe para o aplicativo compilar sem o pipeline.

#include <jni.h>

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo1recomp_NativeRuntime_nativeStart(JNIEnv* env, jclass, jstring, jstring)
{
    return env->NewStringUTF(
        "Esta versao do aplicativo foi compilada sem o codigo recompilado do jogo.");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_bo1recomp_NativeRuntime_nativeState(JNIEnv* env, jclass)
{
    return env->NewStringUTF("sem codigo recompilado");
}

extern "C" JNIEXPORT void JNICALL
Java_com_bo1recomp_NativeRuntime_nativeStop(JNIEnv*, jclass)
{
}
