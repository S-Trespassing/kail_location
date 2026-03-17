package com.kail.location.xposed

import android.os.Bundle
import com.kail.location.utils.KailLog
import kotlin.random.Random

internal object KailCommandHandler {
    private const val PROVIDER = "portal"
    private val keyRef = java.util.concurrent.atomic.AtomicReference<String?>(null)

    fun handle(provider: String?, command: String?, out: Bundle?): Boolean {
        if (provider != PROVIDER) return false
        if (out == null) return false
        if (command.isNullOrBlank()) return false

        if (command == "exchange_key") {
            val key = "k${Random.nextInt(100000, 999999)}${System.nanoTime()}"
            keyRef.set(key)
            out.putString("key", key)
            KailLog.d(null, "XPOSED", "PORTAL接收：交换密钥", isHighFrequency = true)
            return true
        }

        val key = keyRef.get() ?: return false
        if (command != key) return false

        val commandId = out.getString("command_id") ?: return false
        when (commandId) {
            "is_start" -> {
                out.putBoolean("is_start", FakeLocState.isEnabled())
                KailLog.d(null, "XPOSED", "PORTAL接收：查询启动状态 is_start=${FakeLocState.isEnabled()}")
                return true
            }
            "start" -> {
                FakeLocState.setEnabled(true)
                out.putBoolean("started", true)
                out.getDouble("altitude", Double.NaN).let { if (!it.isNaN()) FakeLocState.setAltitude(it) }
                KailLog.d(null, "XPOSED", "PORTAL接收：启动仿真 altitude=${out.getDouble("altitude", Double.NaN)}")
                return true
            }
            "stop" -> {
                FakeLocState.setEnabled(false)
                out.putBoolean("stopped", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：停止仿真")
                return true
            }
            "get_location" -> {
                val loc = FakeLocState.injectInto(null)
                if (loc != null) {
                    out.putDouble("lat", loc.latitude)
                    out.putDouble("lon", loc.longitude)
                    out.putBoolean("ok", true)
                    KailLog.d(null, "XPOSED", "PORTAL接收：获取位置 lat=${loc.latitude} lon=${loc.longitude}", isHighFrequency = true)
                    return true
                }
                KailLog.d(null, "XPOSED", "PORTAL接收：获取位置失败", isHighFrequency = true)
                return false
            }
            "get_listener_size" -> {
                out.putInt("size", LocationServiceHookLite.listenerCount())
                KailLog.d(null, "XPOSED", "PORTAL接收：监听器数量 size=${LocationServiceHookLite.listenerCount()}", isHighFrequency = true)
                return true
            }
            "broadcast_location" -> {
                out.putBoolean("ok", LocationServiceHookLite.broadcastCurrentLocation())
                KailLog.d(null, "XPOSED", "PORTAL接收：广播当前位置 ok=${out.getBoolean("ok", false)}", isHighFrequency = true)
                return true
            }
            "set_speed" -> {
                val speed = out.getFloat("speed", 0f)
                FakeLocState.setSpeed(speed)
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：设置速度 speed=$speed", isHighFrequency = true)
                return true
            }
            "set_bearing" -> {
                val bearing = out.getDouble("bearing", 0.0).toFloat()
                FakeLocState.setBearing(bearing)
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：设置航向 bearing=$bearing", isHighFrequency = true)
                return true
            }
            "set_altitude" -> {
                val altitude = out.getDouble("altitude", Double.NaN)
                if (altitude.isNaN()) return false
                FakeLocState.setAltitude(altitude)
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：设置海拔 altitude=$altitude", isHighFrequency = true)
                return true
            }
            "update_location" -> {
                val lat = out.getDouble("lat", Double.NaN)
                val lon = out.getDouble("lon", Double.NaN)
                if (lat.isNaN() || lon.isNaN()) return false
                FakeLocState.updateLocation(lat, lon)
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：更新位置 lat=$lat lon=$lon", isHighFrequency = true)
                return true
            }
            "set_step_enabled" -> {
                val enabled = out.getBoolean("enabled", false)
                FakeLocState.setStepEnabled(enabled)
                NativeHook.setStepConfigSafe(enabled, FakeLocState.getStepCadence())
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：步频开关 enabled=$enabled", isHighFrequency = true)
                return true
            }
            "set_step_cadence" -> {
                val cadence = out.getFloat("cadence", 0f)
                FakeLocState.setStepCadence(cadence)
                NativeHook.setStepConfigSafe(FakeLocState.isStepEnabled(), cadence)
                out.putBoolean("ok", true)
                KailLog.d(null, "XPOSED", "PORTAL接收：步频 cadence=$cadence", isHighFrequency = true)
                return true
            }
            else -> return false
        }
    }
}

