#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>
#include <dobby.h>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <sys/types.h>

#include "sensor_simulator.h"
#include "elf_util.h"

#define LOG_TAG "NativeHook"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#define SENSOR_TYPE_STEP_COUNTER 19
#define SENSOR_TYPE_STEP_DETECTOR 18

typedef int (*PollFunc)(void*, void*, int);

static PollFunc original_poll = nullptr;
static bool hook_installed = false;

static void process_sensor_events(void* buffer, int count) {
    if (!buffer || count <= 0 || count > 64) return;

    char* base = reinterpret_cast<char*>(buffer);

    ALOGI("event count = %d", count);

    for (int i = 0; i < count; i++) {
        char* evt = base + i * 0x60;

        int version = *(int*)(evt + 0x00);
        int sensor  = *(int*)(evt + 0x04);
        int type    = *(int*)(evt + 0x08);
        int64_t ts  = *(int64_t*)(evt + 0x10);

        float* data = (float*)(evt + 0x18);

        if (type <= 0 || type > 50) continue;

        ALOGI("[#%d] type=%d sensor=%d ver=%d ts=%lld",
              i, type, sensor, version, (long long)ts);

        ALOGI("     data = %.6f %.6f %.6f",
              data[0], data[1], data[2]);

        if (type == 1) {
            ALOGI("     ==> ACCELEROMETER");
        }

        if (type == SENSOR_TYPE_STEP_COUNTER) {
            ALOGI("     ==> STEP_COUNTER = %.0f", data[0]);
        }

        if (type == SENSOR_TYPE_STEP_DETECTOR) {
            ALOGI("     ==> STEP_DETECTOR");
        }
    }
}

extern "C" int hooked_poll(void* thiz, void* buffer, int count) {
    int ret = 0;

    if (original_poll) {
        ret = original_poll(thiz, buffer, count);
    }

    if (!buffer || ret <= 0 || ret > 64) return ret;

    process_sensor_events(buffer, ret);

    return ret;
}

static void install_poll_hook() {
    ALOGI("Installing poll hook using ElfImg...");
    
    ElfImg elf("/system/lib64/libsensorservice.so");
    
    if (!elf.isValid()) {
        ALOGE("Failed to load libsensorservice.so");
        return;
    }
    
    ALOGI("ELF loaded: base=%p", elf.getBase());
    
    // Try to find poll function using prefix lookup
    void* pollAddr = elf.getSymbolAddressByPrefix("HidlSensorHalWrapper::poll");
    
    if (!pollAddr) {
        pollAddr = elf.getSymbolAddressByPrefix("SensorDevice::poll");
    }
    
    if (!pollAddr) {
        pollAddr = elf.getSymbolAddressByPrefix("poll");
    }
    
    if (pollAddr) {
        ALOGI("Found poll at %p", pollAddr);
        
        int ret = DobbyHook(pollAddr, (void*)hooked_poll, (void**)&original_poll);
        
        if (ret == 0) {
            ALOGI("✅ Hook SUCCESS!");
            hook_installed = true;
            return;
        } else {
            ALOGE("DobbyHook failed: %d", ret);
        }
    } else {
        ALOGE("poll symbol not found");
    }
    
    ALOGE("❌ Hook installation FAILED");
}

extern "C" {

JNIEXPORT void JNICALL 
Java_com_kail_location_xposed_FakeLocState_nativeSetGaitParams(
    JNIEnv* env, 
    jclass clazz, 
    jfloat spm, 
    jint mode, 
    jboolean enable
) {
    ALOGI("JNI: Set gait params spm=%.2f, mode=%d, enable=%d", spm, mode, enable ? 1 : 0);
    gait::SensorSimulator::Get().UpdateParams(spm, mode, enable);
}

JNIEXPORT jboolean JNICALL 
Java_com_kail_location_xposed_FakeLocState_nativeReloadConfig(
    JNIEnv* env, 
    jclass clazz
) {
    return gait::SensorSimulator::Get().ReloadConfig() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL 
Java_com_kail_location_xposed_FakeLocState_nativeInitHook(
    JNIEnv* env, 
    jclass clazz
) {
    ALOGI("JNI: init hook (ElfImg)");

    gait::SensorSimulator::Get().Init();
    install_poll_hook();
    gait::SensorSimulator::Get().ReloadConfig();
}

}
