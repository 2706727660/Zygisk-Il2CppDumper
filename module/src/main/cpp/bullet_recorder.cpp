// ============================================================================
// bullet_recorder.cpp —— 记录版 hook（只打日志，不改任何值）
// 用途：把服务器每次"设置武器弹药"和"开火结算"用的 UseItemId / BulletId 打出来
// 接入：Perfare/Zygisk-Il2CppDumper 模板（https://github.com/Perfare/Zygisk-Il2CppDumper）
// 版本对应：香肠派对 25.16(4) dump
// ============================================================================

#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "dobby.h"

#define LOG_TAG "SausageBullet"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// ---------- RVA（libil2cpp.so 内偏移，来自 dump，游戏更新后需重新校对）----------
static constexpr uintptr_t RVA_OnSetWeaponBullet = 0xc7c3ebc; // ServerGPOWeapon_Gun::OnSetWeaponBulletCallBack
static constexpr uintptr_t RVA_OnGunFireBullet   = 0xc7c2d48; // ServerGPOWeapon::OnGunFireCallBack(UseBulletData*)

// ---------- 字段偏移（dump 确认）----------
static constexpr uintptr_t OFF_Ent_BulletCount = 0x10; // SE_GPO::SetWeaponBullet.BulletCount
static constexpr uintptr_t OFF_Ent_UseItemId   = 0x14; // SE_GPO::SetWeaponBullet.UseItemId   ★目标
static constexpr uintptr_t OFF_BD_BulletId     = 0x10; // UseBulletData.BulletId
static constexpr uintptr_t OFF_BD_UseItemId    = 0x14; // UseBulletData.UseItemId
static constexpr uintptr_t OFF_BD_AbilityBullet= 0x18; // UseBulletData.AbilityBulletID (byte)

// ---------- 原函数指针 ----------
using fn_OnSetWeaponBullet = void (*)(void*, void*, void*); // (this, ISystemMsg*, SE_GPO::SetWeaponBullet*)
using fn_OnGunFire         = void (*)(void*, void*);        // (this, UseBulletData*)

static fn_OnSetWeaponBullet orig_SetWeaponBullet = nullptr;
static fn_OnGunFire         orig_GunFire         = nullptr;

// ---------- hook 1：服务器设置武器弹药 ----------
static void hook_SetWeaponBullet(void* self, void* body, void* ent) {
    int32_t useItemId   = *(int32_t*)((uint8_t*)ent + OFF_Ent_UseItemId);
    int32_t bulletCount = *(int32_t*)((uint8_t*)ent + OFF_Ent_BulletCount);
    LOGD("[SetWeaponBullet] UseItemId=%d BulletCount=%d", useItemId, bulletCount);
    orig_SetWeaponBullet(self, body, ent);
}

// ---------- hook 2：服务器开火结算 ----------
static void hook_GunFire(void* self, void* bulletData) {
    int32_t bulletId  = *(int32_t*)((uint8_t*)bulletData + OFF_BD_BulletId);
    int32_t useItemId = *(int32_t*)((uint8_t*)bulletData + OFF_BD_UseItemId);
    uint8_t ability   = *(uint8_t*)((uint8_t*)bulletData + OFF_BD_AbilityBullet);
    LOGD("[GunFire] BulletId=%d UseItemId=%d AbilityBulletID=%d",
         bulletId, useItemId, ability);
    orig_GunFire(self, bulletData);
}

// ---------- 基址获取：读 /proc/self/maps ----------
static uintptr_t get_il2cpp_base() {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512];
    uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "libil2cpp.so")) {
            sscanf(line, "%lx", &base);
            break;
        }
    }
    fclose(f);
    return base;
}

// ---------- 安装入口：libil2cpp.so 加载完成后调用 ----------
extern "C" void install_bullet_recorder() {
    uintptr_t base = get_il2cpp_base();
    if (!base) {
        LOGD("install_bullet_recorder: il2cpp base not found");
        return;
    }
    int r1 = DobbyHook((void*)(base + RVA_OnSetWeaponBullet), (void*)hook_SetWeaponBullet, (void**)&orig_SetWeaponBullet);
    int r2 = DobbyHook((void*)(base + RVA_OnGunFireBullet),   (void*)hook_GunFire,         (void**)&orig_GunFire);
    LOGD("install_bullet_recorder: DobbyHook set=%d fire=%d (base=%p)", r1, r2, (void*)base);
}

// ============================================================================
// 接入 Perfare/Zygisk-Il2CppDumper 模板（只需 2 步）：
//
// 1) 把本文件复制到模板目录 module/src/main/cpp/ 下，并在
//    module/src/main/cpp/CMakeLists.txt 的 add_library(...) 源文件列表里加上
//    bullet_recorder.cpp
//
// 2) 打开 module/src/main/cpp/hack.cpp，在 hack_start() 里
//    il2cpp_api_init(handle); 之后加一行：
//
//        extern "C" void install_bullet_recorder();
//        ...
//        il2cpp_api_init(handle);
//        install_bullet_recorder();      // ★加这一行
//        il2cpp_dump(game_data_dir);
//
// 编译（两种任选）：
//   A. GitHub Actions 云编译：fork 仓库 → Actions → Build → 输入游戏包名
//      com.sofunny.Sausage → Run workflow → 下载 artifact（无需本地环境）
//   B. Android Studio：改 module/src/main/cpp/game.h 的 GamePackageName 宏为
//      com.sofunny.Sausage，跑 gradle 任务 :module:assembleRelease，zip 在 out/
// ============================================================================
