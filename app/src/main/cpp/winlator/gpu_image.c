#include <android/log.h>
#include <android/hardware_buffer.h>
#include <jni.h>
#include <unistd.h>

#define LOG_TAG "GPUImage"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define RGBA_BYTES_PER_PIXEL 4
#define HW_BUFFER_FENCE_TIMEOUT -1
#define HANDSHAKE_BYTE 1
static jmethodID g_setStride = NULL;

JNIEXPORT jlong JNICALL
Java_com_winlator_cmod_renderer_GPUImage_hardwareBufferFromSocket(JNIEnv *env, jclass obj, jint fd) {
    AHardwareBuffer *ahb = NULL;
    uint8_t buf = HANDSHAKE_BYTE;
    if (write(fd, &buf, 1) == -1) {
        LOGE("hardwareBufferFromSocket: write failed");
        return 0;
    }
    if (AHardwareBuffer_recvHandleFromUnixSocket(fd, &ahb) != 0) {
        LOGE("hardwareBufferFromSocket: recvHandle failed");
        return 0;
    }
    AHardwareBuffer_acquire(ahb);
    return (jlong)ahb;
}

JNIEXPORT jlong JNICALL
Java_com_winlator_cmod_renderer_GPUImage_createHardwareBuffer(JNIEnv *env, jclass obj, jshort width, jshort height) {
    if (width <= 0 || height <= 0) return 0;
    AHardwareBuffer_Desc desc = {
        .width  = (uint32_t)width,
        .height = (uint32_t)height,
        .layers = 1,
        .usage  = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE
                | AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN
                | AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY,
        .format = AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM, 
    };
#if __ANDROID_API__ >= 29
    if (AHardwareBuffer_isSupported(&desc) != 0) {
        desc.usage = AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE | AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
        if (AHardwareBuffer_isSupported(&desc) != 0) {
            LOGE("createHardwareBuffer: unsupported desc");
            return 0;
        }
    }
#endif
    AHardwareBuffer *ahb = NULL;
    if (AHardwareBuffer_allocate(&desc, &ahb) != 0) {
        LOGE("createHardwareBuffer: alloc failed");
        return 0;
    }
    return (jlong)ahb;
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_renderer_GPUImage_destroyHardwareBuffer(JNIEnv *env, jclass obj, jlong ptr) {
    AHardwareBuffer *ahb = (AHardwareBuffer *)ptr;
    if (ahb) {
        AHardwareBuffer_release(ahb);
    }
}

JNIEXPORT void JNICALL
Java_com_winlator_cmod_renderer_GPUImage_unlockHardwareBuffer(JNIEnv *env, jclass obj, jlong ptr) {
    AHardwareBuffer *ahb = (AHardwareBuffer *)ptr;
    if (ahb) AHardwareBuffer_unlock(ahb, NULL);
}

JNIEXPORT jobject JNICALL
Java_com_winlator_cmod_renderer_GPUImage_lockHardwareBuffer(JNIEnv *env, jclass obj, jlong ptr) {
    AHardwareBuffer *ahb = (AHardwareBuffer *)ptr;
    if (!ahb) {
        LOGE("lockHardwareBuffer: null pointer");
        return NULL;
    }
    void *addr;
    if (AHardwareBuffer_lock(ahb, AHARDWAREBUFFER_USAGE_CPU_WRITE_OFTEN, HW_BUFFER_FENCE_TIMEOUT, NULL, &addr) != 0) {
        LOGE("lockHardwareBuffer: lock failed");
        return NULL;
    }
    if (!addr) {
        LOGE("lockHardwareBuffer: null address");
        AHardwareBuffer_unlock(ahb, NULL);
        return NULL;
    }
    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(ahb, &desc);

    if (!g_setStride) {
        jclass cls = (*env)->GetObjectClass(env, obj);
        g_setStride = (*env)->GetMethodID(env, cls, "setStride", "(S)V");
        if (!g_setStride) return 0;
        if ((*env)->ExceptionCheck(env)) {
            (*env)->ExceptionClear(env);
            return 0;
        }
    }
    jshort stride_jshort = desc.stride > 32767 ? 32767 : (jshort)desc.stride;
    (*env)->CallVoidMethod(env, obj, g_setStride, stride_jshort);

    jobject buffer = (*env)->NewDirectByteBuffer(env, addr, (jlong)desc.stride * (jlong)desc.height * RGBA_BYTES_PER_PIXEL);
    if (!buffer) {
        LOGE("lockHardwareBuffer: NewDirectByteBuffer failed");
        AHardwareBuffer_unlock(ahb, NULL);
    }
    return buffer;
}
