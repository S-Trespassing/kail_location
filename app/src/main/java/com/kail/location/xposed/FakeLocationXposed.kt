package com.kail.location.xposed

import de.robv.android.xposed.IXposedHookLoadPackage
import de.robv.android.xposed.IXposedHookZygoteInit
import de.robv.android.xposed.callbacks.XC_LoadPackage
import com.kail.location.utils.KailLog
import android.util.Log
import java.util.concurrent.atomic.AtomicBoolean

class FakeLocationXposed : IXposedHookLoadPackage, IXposedHookZygoteInit {
    private val firstHandleRef = AtomicBoolean(false)

    override fun initZygote(startupParam: IXposedHookZygoteInit.StartupParam?) {
        KailLog.d(null, "XPOSED", "初始化Zygote")
    }

    override fun handleLoadPackage(lpparam: XC_LoadPackage.LoadPackageParam?) {
        val pkg = lpparam?.packageName ?: return
        val process = lpparam.processName ?: ""

        val allowedPkgs = setOf(
            "android",
            "com.android.phone",
            "com.android.location.fused",
            "com.google.android.gms",
            "com.xiaomi.location.fused",
            "com.oplus.location",
            "com.vivo.location",
            "com.qualcomm.location",
            "com.tencent.android.location",
        )
        
        if (pkg in allowedPkgs) {
            KailLog.d(null, "XPOSED", "加载进程 pkg=$pkg process=$process")
        }

        if (firstHandleRef.compareAndSet(false, true)) {
            KailLog.d(null, "XPOSED", "首次处理加载 pkg=$pkg process=$process")
        }
        if (pkg !in allowedPkgs) return

        val injectedKey = "kail_location.injected_$pkg"
        if (System.getProperty(injectedKey) == "true") {
            KailLog.d(null, "XPOSED", "已注入 pkg=$pkg process=$process")
            return
        }
        System.setProperty(injectedKey, "true")

        KailLog.d(null, "XPOSED", "开始注入 pkg=$pkg process=$process")

        val systemClassLoader = kotlin.runCatching {
            val atClz = kotlin.runCatching {
                lpparam.classLoader.loadClass("android.app.ActivityThread")
            }.getOrNull() ?: Class.forName("android.app.ActivityThread")
            val at = atClz.getMethod("currentActivityThread").invoke(null)
            at.javaClass.classLoader
        }.getOrNull()

        kotlin.runCatching {
            val cl = systemClassLoader ?: lpparam.classLoader
            KailLog.d(null, "XPOSED", "开始hook pkg=$pkg process=$process")
            LocationServiceHookLite.hook(cl)
            ThirdPartyLocationHookLite.hook(cl)
            SensorHookLite.hook(cl)
            
            // 只有在 system_server (pkg="android") 中才初始化 Native Hook
            if (pkg == "android") {
                NativeHook.startHook()
                KailLog.i(null, "XPOSED", "Native Hook 尝试启动 (system_server)")
            }

            KailLog.d(null, "XPOSED", "hook完成")
        }.onFailure {
            KailLog.e(null, "XPOSED", "hook失败: ${it.message}")
            KailLog.e(null, "XPOSED", Log.getStackTraceString(it))
        }
    }
}
