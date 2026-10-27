#include "config.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "log.h"

Config g_config;

static float ReadFloat(const char* path, const char* key, float fallback) {
    char buffer[64], def[64];
    snprintf(def, sizeof(def), "%g", fallback);
    GetPrivateProfileStringA("VR", key, def, buffer, sizeof(buffer), path);
    return (float)atof(buffer);
}

void LoadConfig(const char* path) {
    Config& c = g_config;
    c.enabled = GetPrivateProfileIntA("VR", "Enabled", c.enabled, path) != 0;
    c.stereo = GetPrivateProfileIntA("VR", "Stereo", c.stereo, path) != 0;
    c.positional = GetPrivateProfileIntA("VR", "Positional", c.positional, path) != 0;
    c.unitsPerMetre = ReadFloat(path, "UnitsPerMetre", c.unitsPerMetre);
    c.ipdScale = ReadFloat(path, "IpdScale", c.ipdScale);
    c.fovOverride = ReadFloat(path, "FovOverride", c.fovOverride);
    c.headsetFov = ReadFloat(path, "HeadsetFov", c.headsetFov);
    if (c.headsetFov < 60 || c.headsetFov > 130) c.headsetFov = 100;
    c.fovOffset = GetPrivateProfileIntA("VR", "FovOffset", c.fovOffset, path);
    c.logPoses = GetPrivateProfileIntA("VR", "LogPoses", c.logPoses, path) != 0;
    c.turnSpeed = ReadFloat(path, "TurnSpeed", c.turnSpeed);
    c.snapTurn = GetPrivateProfileIntA("VR", "SnapTurn", c.snapTurn, path) != 0;
    c.snapAngle = ReadFloat(path, "SnapAngle", c.snapAngle);
    c.menuFrames = GetPrivateProfileIntA("VR", "MenuFrames", c.menuFrames, path);
    c.theaterFov = ReadFloat(path, "TheaterFov", c.theaterFov);
    c.pointerMinCutoff = ReadFloat(path, "PointerMinCutoff", c.pointerMinCutoff);
    c.pointerBeta = ReadFloat(path, "PointerBeta", c.pointerBeta);
    c.noBob = GetPrivateProfileIntA("VR", "NoBob", 1, path) != 0;
    c.bobLog = GetPrivateProfileIntA("VR", "BobLog", 0, path) != 0;
    c.gpuTiming = GetPrivateProfileIntA("VR", "GpuTiming", 0, path) != 0;
    c.cpuTiming = GetPrivateProfileIntA("VR", "CpuTiming", 0, path) != 0;
    c.rawPose = GetPrivateProfileIntA("VR", "RawPose", 1, path) != 0;
    c.gameFpsCap = ReadFloat(path, "GameFpsCap", c.gameFpsCap);
    c.syncFromCamera = GetPrivateProfileIntA("VR", "SyncFromCamera", 1, path) != 0;
    c.syncPixel = GetPrivateProfileIntA("VR", "SyncPixel", 1, path) != 0;
    c.menuMouseScale = ReadFloat(path, "MenuMouseScale", c.menuMouseScale);
    c.menuMouseScaleX = ReadFloat(path,"MenuMouseScaleX",c.menuMouseScaleX);
    c.menuMouseScaleY = ReadFloat(path,"MenuMouseScaleY",c.menuMouseScaleY);
    if(!isfinite(c.menuMouseScaleX)||c.menuMouseScaleX<.1f||c.menuMouseScaleX>20)c.menuMouseScaleX=1.92f;
    if(!isfinite(c.menuMouseScaleY)||c.menuMouseScaleY<.1f||c.menuMouseScaleY>20)c.menuMouseScaleY=1.44f;
    c.weaponInHand = GetPrivateProfileIntA("VR", "WeaponInHand", c.weaponInHand, path) != 0;
    c.plasmidInHand = GetPrivateProfileIntA("VR", "PlasmidInHand", c.plasmidInHand, path) != 0;
    c.rigidWeapon = GetPrivateProfileIntA("VR", "RigidWeapon", c.rigidWeapon, path) != 0;
    c.weaponGripAnchor = GetPrivateProfileIntA("VR", "WeaponGripAnchor", c.weaponGripAnchor, path) != 0;
    c.weaponGripOffset.x = ReadFloat(path,"WeaponGripForward",c.weaponGripOffset.x);
    c.weaponGripOffset.y = ReadFloat(path,"WeaponGripRight",c.weaponGripOffset.y);
    c.weaponGripOffset.z = ReadFloat(path,"WeaponGripUp",c.weaponGripOffset.z);
    c.hideCrosshair = GetPrivateProfileIntA("VR","HideCrosshair",c.hideCrosshair,path) != 0;
    c.aimReticles = GetPrivateProfileIntA("VR","AimReticles",c.aimReticles,path) != 0;
    c.weaponGeometry = GetPrivateProfileIntA("VR","WeaponGeometry",c.weaponGeometry,path) != 0;
    c.weaponNearScale=ReadFloat(path,"WeaponNearScale",c.weaponNearScale);
    if(!isfinite(c.weaponNearScale)||c.weaponNearScale<=0||c.weaponNearScale>1)c.weaponNearScale=.1f;
    c.weaponPitch = ReadFloat(path, "WeaponPitch", c.weaponPitch);
    c.weaponOffset.x = ReadFloat(path, "WeaponOffsetForward", c.weaponOffset.x);
    c.weaponOffset.y = ReadFloat(path, "WeaponOffsetRight", c.weaponOffset.y);
    c.weaponOffset.z = ReadFloat(path, "WeaponOffsetUp", c.weaponOffset.z);
    c.vignette = GetPrivateProfileIntA("VR", "Vignette", c.vignette, path) != 0;
    c.gameHands = GetPrivateProfileIntA("VR", "GameHands", c.gameHands, path) != 0;
    c.mouseInject = GetPrivateProfileIntA("VR", "MouseInject", c.mouseInject, path) != 0;
    c.theaterDraw = GetPrivateProfileIntA("VR", "TheaterDraw", c.theaterDraw, path) != 0;
    c.debugClickU = ReadFloat(path, "DebugClickU", c.debugClickU);
    c.debugClickV = ReadFloat(path, "DebugClickV", c.debugClickV);
    c.debugClickPeriod = ReadFloat(path, "DebugClickPeriod", c.debugClickPeriod);
    c.depthStereo = GetPrivateProfileIntA("VR", "DepthStereo", c.depthStereo, path) != 0;
    c.lazyDepth = GetPrivateProfileIntA("VR", "LazyDepth", c.lazyDepth, path) != 0;
    // Depth reconstruction requires a centre camera; alternating eye offsets
    // would apply IPD twice and give the two reconstructed eyes different poses.
    if (c.depthStereo) c.stereo = false;
    {
        char buf[64];
        GetPrivateProfileStringA("VR", "GameResolution", "0x0", buf, sizeof(buf), path);
        unsigned w = 0, h = 0;
        if (sscanf(buf, "%ux%u", &w, &h) == 2 && w >= 320 && h >= 240) c.gameWidth = w, c.gameHeight = h;
    }
    {
        char buf[64];unsigned w=0,h=0;
        GetPrivateProfileStringA("VR","WorldBackbuffer","0x0",buf,sizeof(buf),path);
        if(sscanf(buf,"%ux%u",&w,&h)==2&&w>=1280&&w<=4096&&h>=720&&h<=2160){
            c.worldBackWidth=w;c.worldBackHeight=h;
        }
    }
    c.screenDistance = ReadFloat(path, "ScreenDistance", c.screenDistance);
    c.worldWindowed=GetPrivateProfileIntA("VR","WorldWindowed",0,path)!=0;
    c.dynamicRes = GetPrivateProfileIntA("VR", "DynamicRes", c.dynamicRes, path) != 0;
    c.dynamicMin = GetPrivateProfileIntA("VR", "DynamicMin", c.dynamicMin, path);
    c.depthDebug = GetPrivateProfileIntA("VR", "DepthDebug", c.depthDebug, path) != 0;
    c.depthStrength = ReadFloat(path, "DepthStrength", c.depthStrength);
    c.depthMaxShift = ReadFloat(path, "DepthMaxShift", c.depthMaxShift);
    c.depthNearSoftness = ReadFloat(path,"DepthNearSoftness",c.depthNearSoftness);
    if(!isfinite(c.depthNearSoftness)||c.depthNearSoftness<0||c.depthNearSoftness>0.95f)c.depthNearSoftness=0;
    if (!isfinite(c.depthStrength) || c.depthStrength < 0 || c.depthStrength > 2) c.depthStrength = 1.0f;
    if (!isfinite(c.depthMaxShift) || c.depthMaxShift < 0 || c.depthMaxShift > 0.1f) c.depthMaxShift = 0.035f;
    c.menuAspect = ReadFloat(path, "MenuAspect", c.menuAspect);
    c.screenWidthDeg = ReadFloat(path, "ScreenWidth", c.screenWidthDeg);
    c.roomScale = GetPrivateProfileIntA("VR", "RoomScale", c.roomScale, path) != 0;
    c.realHeight = GetPrivateProfileIntA("VR", "RealHeight", c.realHeight, path) != 0;
    c.playerHeight = ReadFloat(path, "PlayerHeight", c.playerHeight);
    c.debugDump = GetPrivateProfileIntA("VR", "DebugDump", c.debugDump, path) != 0;
    c.devHarness = GetPrivateProfileIntA("VR","DevHarness",0,path)!=0;
    c.hudInMenu = GetPrivateProfileIntA("VR", "HudInMenu", c.hudInMenu, path) != 0;
    c.swingMelee = GetPrivateProfileIntA("VR", "SwingMelee", c.swingMelee, path) != 0;
    c.swingSpeed = ReadFloat(path, "SwingSpeed", c.swingSpeed);
    c.menuHands = GetPrivateProfileIntA("VR", "MenuHands", c.menuHands, path) != 0;
    c.menuStereo = GetPrivateProfileIntA("VR", "MenuStereo", c.menuStereo, path) != 0;
    c.nativeCinema = GetPrivateProfileIntA("VR", "NativeCinema", c.nativeCinema, path) != 0;
    c.screenMipmaps = GetPrivateProfileIntA("VR", "ScreenMipmaps", c.screenMipmaps, path) != 0;
    c.menuProps = GetPrivateProfileIntA("VR", "MenuProps", c.menuProps, path) != 0;
    c.hideArms = GetPrivateProfileIntA("VR", "HideArms", c.hideArms, path) != 0;
    {
        char buf[32];
        GetPrivateProfileStringA("VR", "ArmsDrawSite", "0x10b917b4", buf, sizeof(buf), path);
        c.armsDrawSite = (unsigned)strtoul(buf, nullptr, 16);
    }
    {
        char buf[128];
        GetPrivateProfileStringA("VR", "ArmsVertices", "", buf, sizeof(buf), path);
        char* p = buf;
        for (int i = 0; i < 8 && *p; i++) {
            c.armsVertices[i] = (unsigned)strtoul(p, &p, 10);
            while (*p == ',' || *p == ' ') p++;
        }
    }
    c.hideMask = GetPrivateProfileIntA("VR", "HideMask", c.hideMask, path);
    c.plasmidRay = GetPrivateProfileIntA("VR", "PlasmidRay", c.plasmidRay, path) != 0;
    c.flatMenus = GetPrivateProfileIntA("VR", "FlatMenus", c.flatMenus, path) != 0;
    c.hudScale = ReadFloat(path, "HudScale", c.hudScale);
    if (c.hudScale < 0.5f || c.hudScale > 3.0f) c.hudScale = 1.0f;
    c.wristHud = GetPrivateProfileIntA("VR", "WristHud", c.wristHud, path) != 0;
    c.gameplayHud = GetPrivateProfileIntA("VR", "GameplayHud", c.gameplayHud, path) != 0;
    c.hudDashboard = GetPrivateProfileIntA("VR", "HudDashboard", c.hudDashboard, path) != 0;
    c.hudVrText = GetPrivateProfileIntA("VR", "HudVrText", c.hudVrText, path) != 0;
    c.hudMessagesOnLeft = GetPrivateProfileIntA("VR", "HudMessagesOnLeft", c.hudMessagesOnLeft, path) != 0;
    c.hudSupersample = ReadFloat(path,"HudSupersample",1);
    if(!isfinite(c.hudSupersample)||c.hudSupersample<1||c.hudSupersample>2)c.hudSupersample=1;
    c.navigationDistance=ReadFloat(path,"NavigationDistance",2.f);
    if(!isfinite(c.navigationDistance)||c.navigationDistance<1.5f||c.navigationDistance>3)c.navigationDistance=2.f;
    c.hideAttackTutorial = GetPrivateProfileIntA("VR", "HideAttackTutorial", c.hideAttackTutorial, path) != 0;
    c.ammoOnLeft = GetPrivateProfileIntA("VR", "AmmoOnLeft", c.ammoOnLeft, path) != 0;
    c.questHealingButtons = GetPrivateProfileIntA("VR", "QuestHealingButtons", c.questHealingButtons, path) != 0;
    c.questHackGrip = GetPrivateProfileIntA("VR", "QuestHackGrip", c.questHackGrip, path) != 0;
    c.instantEve = GetPrivateProfileIntA("VR", "InstantEve", c.instantEve, path) != 0;
    c.questControllerBindings = GetPrivateProfileIntA("VR", "QuestControllerBindings", c.questControllerBindings, path) != 0;
    c.navigationGeometry = GetPrivateProfileIntA("VR", "NavigationGeometry", c.navigationGeometry, path) != 0;
    c.noWeaponAnimations = GetPrivateProfileIntA("VR", "NoWeaponAnimations", c.noWeaponAnimations, path) != 0;
    c.wrenchScale=ReadFloat(path,"WrenchScale",1);
    if(!isfinite(c.wrenchScale)||c.wrenchScale<.5f||c.wrenchScale>1.5f)c.wrenchScale=1;
    c.wrenchForward=ReadFloat(path,"WrenchForward",0);
    if(!isfinite(c.wrenchForward)||c.wrenchForward<0||c.wrenchForward>.25f)c.wrenchForward=0;
    c.hideDamageOverlay=GetPrivateProfileIntA("VR","HideDamageOverlay",0,path)!=0;
    c.meleePitchServo=GetPrivateProfileIntA("VR","MeleePitchServo",0,path)!=0;
    c.worldPosePin = GetPrivateProfileIntA("VR", "WorldPosePin", c.worldPosePin, path) != 0;
#ifdef BIOSHOCK_RELEASE
    c.logPoses=c.gpuTiming=c.cpuTiming=c.debugDump=c.depthDebug=c.bobLog=c.devHarness=false;
    c.debugClickU=c.debugClickV=-1;
#endif
    Log("Config: enabled=%d stereo=%d positional=%d units/m=%.1f ipdScale=%.2f fov=%.1f fovOffset=0x%x",
        c.enabled, c.stereo, c.positional, c.unitsPerMetre, c.ipdScale, c.fovOverride, c.fovOffset);
}
