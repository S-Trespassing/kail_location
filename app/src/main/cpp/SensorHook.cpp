#include <jni.h>
#include <android/log.h>
#include <shadowhook.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "GaitSimulation.h"
#include "elf_util.h"

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

static bool enableSensorHook = false;

// 原始函数指针
typedef ssize_t (*sensor_device_poll_t)(void* self, sensors_event_t* buffer, size_t count);
static sensor_device_poll_t orig_poll = nullptr;

typedef int64_t (*OriginalSensorEventQueueWriteType)(void*, void*, int64_t);
static OriginalSensorEventQueueWriteType OriginalSensorEventQueueWrite = nullptr;

typedef void (*OriginalConvertToSensorEventType)(void*, void*);
static OriginalConvertToSensorEventType OriginalConvertToSensorEvent = nullptr;

static void* hook_stub_poll = nullptr;
static void* hook_stub_write = nullptr;
static void* hook_stub_convert = nullptr;

void log_sensor_event(const char* prefix, const sensors_event_t& event) {
    LOGD("%s: sensor=%d, type=%d, timestamp=%lld, data=[%.2f, %.2f, %.2f, ...]", 
         prefix, event.sensor, event.type, (long long)event.timestamp, 
         event.data[0], event.data[1], event.data[2]);
}

// Hook 后的代理函数
ssize_t proxy_poll(void* self, sensors_event_t* buffer, size_t count) {
    ssize_t result = orig_poll(self, buffer, count);
    if (result <= 0) return result;

    if (enableSensorHook) {
        GaitSimulation& sim = GaitSimulation::getInstance();
        for (ssize_t i = 0; i < result; ++i) {
            sensors_event_t& event = buffer[i];
            log_sensor_event("Poll Event (Before)", event);
            
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
                    }
                    break;
            }
            log_sensor_event("Poll Event (After)", event);
        }
    }
    return result;
}

int64_t proxy_SensorEventQueueWrite(void *tube, void *events, int64_t numEvents) {
    if (enableSensorHook) {
        sensors_event_t* evs = (sensors_event_t*)events;
        for (int64_t i = 0; i < numEvents; ++i) {
            log_sensor_event("Write Event", evs[i]);
        }
    }
    return OriginalSensorEventQueueWrite(tube, events, numEvents);
}

void proxy_ConvertToSensorEvent(void *src, void *dst) {
    if (enableSensorHook) {
        // Portal logic migrated
        auto a = *(int32_t *)((char*)src + 4);
        auto b = *(int32_t *)((char*)src + 8);
        auto c = *(int64_t *)((char*)src + 16);

        *(int64_t *)((char*)dst + 16) = 0LL;
        *(int32_t *)((char*)dst + 24) = 0;
        *(int64_t *)((char*)dst) = c;
        *(int32_t *)((char*)dst + 8) = a;
        *(int32_t *)((char*)dst + 12) = b;
        *(int8_t *)((char*)dst + 28) = b;

        if (b == 18) {
            *(float *)((char*)dst + 16) = -1.0;
        } else if (b == 19) {
            *(int64_t *)((char*)dst + 16) = -1;
        } else {
            *(float *)((char*)dst + 16) = -1.0;
            *(float *)((char*)dst + 24) = -1.0;
            *(int8_t *)((char*)dst + 28) = *(int8_t *)((char*)src + 36);
        }
        log_sensor_event("Converted Event", *(sensors_event_t*)dst);
    } else {
        OriginalConvertToSensorEvent(src, dst);
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_com_kail_location_xposed_NativeHook_initHook(JNIEnv* env, jclass clazz) {
    // 初始化 ShadowHook
    shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);

    const char* library = "libsensorservice.so";
    SandHook::ElfImg sensorService(library);
    if (!sensorService.isValid()) {
        LOGE("Failed to load libsensorservice.so via ElfImg");
        // Fallback to library name for shadowhook
    }

    // 1. Hook SensorDevice::poll
    if (orig_poll == nullptr) {
        const char* symbol_poll = "_ZN7android12SensorDevice4pollEPNS_13sensors_event_tEm";
        void* addr = sensorService.isValid() ? sensorService.getSymbolAddress<void*>(symbol_poll) : nullptr;
        
        if (addr) {
            hook_stub_poll = shadowhook_hook_func_addr(addr, (void*)proxy_poll, (void**)&orig_poll);
        } else {
            hook_stub_poll = shadowhook_hook_sym_name(library, symbol_poll, (void*)proxy_poll, (void**)&orig_poll);
        }
        
        if (hook_stub_poll) LOGD("Hook SensorDevice::poll success!");
        else LOGE("Hook SensorDevice::poll failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // 2. Hook SensorEventQueue::write
    if (OriginalSensorEventQueueWrite == nullptr) {
        const char* symbol_write = "_ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventm";
        void* addr = sensorService.isValid() ? sensorService.getSymbolAddress<void*>(symbol_write) : nullptr;
        if (!addr && sensorService.isValid()) {
            symbol_write = "_ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventj";
            addr = sensorService.getSymbolAddress<void*>(symbol_write);
        }

        if (addr) {
            hook_stub_write = shadowhook_hook_func_addr(addr, (void*)proxy_SensorEventQueueWrite, (void**)&OriginalSensorEventQueueWrite);
        } else {
            hook_stub_write = shadowhook_hook_sym_name(library, symbol_write, (void*)proxy_SensorEventQueueWrite, (void**)&OriginalSensorEventQueueWrite);
            if (!hook_stub_write) {
                symbol_write = "_ZN7android16SensorEventQueue5writeERKNS_2spINS_7BitTubeEEEPK12ASensorEventj";
                hook_stub_write = shadowhook_hook_sym_name(library, symbol_write, (void*)proxy_SensorEventQueueWrite, (void**)&OriginalSensorEventQueueWrite);
            }
        }
        
        if (hook_stub_write) LOGD("Hook SensorEventQueue::write success!");
        else LOGE("Hook SensorEventQueue::write failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    // 3. Hook convertToSensorEvent
    if (OriginalConvertToSensorEvent == nullptr) {
        const char* symbol_convert = "_ZN7android8hardware7sensors4V1_014implementation20convertToSensorEventERKNS2_5EventEP15sensors_event_t";
        void* addr = sensorService.isValid() ? sensorService.getSymbolAddress<void*>(symbol_convert) : nullptr;
        
        if (addr) {
            hook_stub_convert = shadowhook_hook_func_addr(addr, (void*)proxy_ConvertToSensorEvent, (void**)&OriginalConvertToSensorEvent);
        } else {
            hook_stub_convert = shadowhook_hook_sym_name(library, symbol_convert, (void*)proxy_ConvertToSensorEvent, (void**)&OriginalConvertToSensorEvent);
        }
        
        if (hook_stub_convert) LOGD("Hook convertToSensorEvent success!");
        else LOGE("Hook convertToSensorEvent failed: %s", shadowhook_to_errmsg(shadowhook_get_errno()));
    }

    return 0;
}

extern "C" JNIEXPORT void JNICALL
Java_com_kail_location_xposed_NativeHook_setStepConfig(JNIEnv* env, jclass clazz, jboolean enabled, jfloat stepsPerMinute) {
    enableSensorHook = enabled;
    GaitSimulation::getInstance().setEnabled(enabled);
    GaitSimulation::getInstance().setStepFrequency(stepsPerMinute);
    LOGD("Native setStepConfig: enabled=%d, spm=%.2f", enabled, stepsPerMinute);
}
