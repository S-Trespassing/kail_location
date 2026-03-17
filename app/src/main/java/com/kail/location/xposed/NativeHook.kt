package com.kail.location.xposed

import com.kail.location.utils.KailLog

object NativeHook {
    private var isLoaded = false

    init {
        try {
            System.loadLibrary("kail_native_hook")
            isLoaded = true
            KailLog.i(null, "NativeHook", "Native library loaded successfully")
        } catch (e: Throwable) {
            KailLog.e(null, "NativeHook", "Failed to load native library: ${e.message}")
        }
    }

    /**
     * 初始化 Hook
     * 建议在 system_server 进程中调用
     */
    external fun initHook(): Int

    /**
     * 设置步频配置
     * @param enabled 是否开启模拟
     * @param stepsPerMinute 步频 (步/分钟)
     */
    external fun setStepConfig(enabled: Boolean, stepsPerMinute: Float)

    fun startHook() {
        if (isLoaded) {
            val res = initHook()
            KailLog.i(null, "NativeHook", "initHook result: $res")
        } else {
            KailLog.e(null, "NativeHook", "startHook failed: library not loaded")
        }
    }

    fun setStepConfigSafe(enabled: Boolean, stepsPerMinute: Float) {
        if (isLoaded) {
            try {
                setStepConfig(enabled, stepsPerMinute)
            } catch (e: Throwable) {
                KailLog.e(null, "NativeHook", "setStepConfig failed: ${e.message}")
            }
        } else {
            KailLog.w(null, "NativeHook", "setStepConfig skipped: library not loaded")
        }
    }
}
