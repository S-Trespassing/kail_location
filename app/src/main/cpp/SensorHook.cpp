#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <stdint.h>
#include <string.h>
#include "GaitSimulation.h"

#define LOG_TAG "KailNativeHook"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 传感器事件结构体
typedef struct sensors_event_t {
    int32_t version;
    int32_t sensor;
    int32_t type;
    int32_t reserved0;
    int64_t timestamp;
    union {
        float data[16];
        struct {
            float x;
            float y;
            float z;
        } acceleration;
    };
    int32_t reserved1[4];
} sensors_event_t;

// 传感器类型定义
#define SENSOR_TYPE_ACCELEROMETER 1
#define SENSOR_TYPE_STEP_COUNTER 19
#define SENSOR_TYPE_STEP_DETECTOR 18

// 原始函数指针
typedef ssize_t (*sensor_device_poll_t)(void* self, sensors_event_t* buffer, size_t count);
static sensor_device_poll_t orig_poll = nullptr;
static void* hook_stub = nullptr;

// Hook 后的代理函数
ssize_t proxy_poll(void* self, sensors_event_t* buffer, size_t count) {
    // 调用原始函数
    ssize_t result = orig_poll(self, buffer, count);
    
    if (result <= 0) return result;

    GaitSimulation& sim = GaitSimulation::getInstance();

    // 处理返回的传感器事件
    for (ssize_t i = 0; i < result; ++i) {
        sensors_event_t& event = buffer[i];
        
        switch (event.type) {
            case SENSOR_TYPE_ACCELEROMETER:
                sim.generateAccelerometer(event.timestamp, event.data);
                break;
            case SENSOR_TYPE_STEP_COUNTER:
                event.data[0] = (float)sim.updateStepCounter(event.timestamp);
                break;
            case SENSOR_TYPE_STEP_DETECTOR:
                if (sim.consumeStepDetected()) {
                    event.data[0] = 1.0f;
                } else {
                    // 如果原始事件不是步数检测，但我们需要模拟步数检测，
                    // 这里通常不直接修改，而是通过拦截其他事件或在没有事件时注入。
                    // 但对于 poll 拦截，通常只修改已有事件。
                }
                break;
        }
    }

    return result;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kail_location_xposed_NativeHook_initHook(JNIEnv* env, jclass clazz) {
    if (orig_poll != nullptr) return 0;

    // 初始化 ShadowHook
    shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);

    // Target symbol: _ZN7android12SensorDevice4pollEPNS_13sensors_event_tEm
    // SensorDevice::poll on Android 10-14
    const char* symbol = "_ZN7android12SensorDevice4pollEPNS_13sensors_event_tEm";
    const char* library = "libsensorservice.so";

    hook_stub = shadowhook_hook_sym_name(
        library, 
        symbol, 
        (void*)proxy_poll, 
        (void**)&orig_poll
    );

    if (hook_stub == nullptr) {
        int err_num = shadowhook_get_errno();
        LOGE("Hook failed: %d, %s", err_num, shadowhook_to_errmsg(err_num));
        return -1;
    }

    LOGD("Hook SensorDevice::poll success!");
    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kail_location_xposed_NativeHook_setStepConfig(JNIEnv* env, jclass clazz, jboolean enabled, jfloat stepsPerMinute) {
    GaitSimulation::getInstance().setEnabled(enabled);
    GaitSimulation::getInstance().setStepFrequency(stepsPerMinute);
    LOGD("Native setStepConfig: enabled=%d, spm=%.2f", enabled, stepsPerMinute);
}
