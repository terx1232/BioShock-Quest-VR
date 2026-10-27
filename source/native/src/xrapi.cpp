#include "xrapi.h"

#include <winsock2.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "devharness.h"
#include "log.h"
#include "pcvr.h"
#include "posewire.h"
#include "config.h"

namespace xrapi {
namespace {

// WinlatorXR sends the headset state to these (XrVersion04.getPortsOut) and listens on kPortIn
const unsigned short kPortsOut[] = {7872, 7873, posewire::kPort, 7876};
const unsigned short kPortIn = 7278;

CRITICAL_SECTION g_lock;
State g_state = {};
SOCKET g_send = INVALID_SOCKET;
volatile LONG g_graphicsReady = 0;
volatile LONG g_rawTick = 0;
volatile LONG g_cinemaTick = 0;
volatile LONG g_worldPinTick = 0;
volatile LONG g_exactPoseTick = 0;
volatile LONG g_hudTick=0,g_hudReadyTick=0;
int g_pointerX=-1,g_pointerY=-1;bool g_pointerDown=false;DWORD g_pointerTick=0;

// "client0 <28 numbers> <buttons TF...> <altitude> <left grip quat> <right grip quat> <flags>"
bool Parse(char* text, State& s) {
    char* save = nullptr;
    char* tok = strtok_s(text, " \t\r\n", &save);
    if (!tok || strncmp(tok, "client", 6) != 0) return false;

    float f[28];
    for (int i = 0; i < 28; i++) {
        tok = strtok_s(nullptr, " \t\r\n", &save);
        if (!tok) return false;
        f[i] = (float)atof(tok);
    }
    s.left = {{f[0], f[1], f[2], f[3]}, {f[0], f[1], f[2], f[3]}, {f[6], f[7], f[8]}, f[4], f[5]};
    s.right = {{f[9], f[10], f[11], f[12]}, {f[9], f[10], f[11], f[12]}, {f[15], f[16], f[17]}, f[13], f[14]};
    s.hmdOrientation = {f[18], f[19], f[20], f[21]};
    s.hmdPosition = {f[22], f[23], f[24]};
    s.ipd = f[25];
    s.fovX = f[26];
    s.fovY = f[27];

    tok = strtok_s(nullptr, " \t\r\n", &save);  // frame sync index
    if (!tok) return false;
    s.sync = atoi(tok);

    tok = strtok_s(nullptr, " \t\r\n", &save);  // buttons
    if (!tok) return false;
    for (int i = 0; i < BUTTON_COUNT; i++) s.buttons[i] = tok[i] == 'T';

    // XrAPI 0.5: высота над полом и ориентации рукояток левого и правого контроллера
    float g[9];
    for (int i = 0; i < 9; i++) {
        tok = strtok_s(nullptr, " \t\r\n", &save);
        if (!tok) return true;
        g[i] = (float)atof(tok);
    }
    s.altitude = g[0];
    if (g[4] != 0 || g[1] != 0 || g[2] != 0 || g[3] != 0) s.left.grip = {g[1], g[2], g[3], g[4]};
    if (g[8] != 0 || g[5] != 0 || g[6] != 0 || g[7] != 0) s.right.grip = {g[5], g[6], g[7], g[8]};
    return true;
}

DWORD WINAPI Listen(LPVOID param) {
    SOCKET sock = (SOCKET)(UINT_PTR)param;
    char buffer[2048];
    for (;;) {
        int n = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            Sleep(1);
            continue;
        }
        buffer[n] = 0;
        if(n==8&&!memcmp(buffer,"BSHU",4)){
            unsigned ready=0;memcpy(&ready,buffer+4,4);
            InterlockedExchange(&g_hudTick,GetTickCount());
            InterlockedExchange(&g_hudReadyTick,ready==1?GetTickCount():0);continue;
        }
        if(n==8&&!memcmp(buffer,"BSWP\1\0\0\0",8)){
            InterlockedExchange(&g_worldPinTick,GetTickCount());continue;
        }
        if(n==8&&!memcmp(buffer,"BSWP\2\0\0\0",8)){
            InterlockedExchange(&g_exactPoseTick,GetTickCount());continue;
        }
        if((n==8||n==20)&&!memcmp(buffer,"BSQA",4)) {
            unsigned ready=0;memcpy(&ready,buffer+4,4);
            InterlockedExchange(&g_cinemaTick,ready==1?GetTickCount():0);
            if(n==20){
                int x,y,down;memcpy(&x,buffer+8,4);memcpy(&y,buffer+12,4);memcpy(&down,buffer+16,4);
                EnterCriticalSection(&g_lock);
                if(x>=0&&y>=0&&x<8192&&y<8192&&(down==0||down==1)){
                    g_pointerX=x;g_pointerY=y;g_pointerDown=ready==1&&down;g_pointerTick=GetTickCount();
                }
                LeaveCriticalSection(&g_lock);
            }
            continue;
        }
        bool rawPacket = n >= 4 && (!memcmp(buffer, "BSV\1", 4)||!memcmp(buffer,"BSV\2",4));
        if (rawPacket && !g_config.rawPose) continue;
        State s;
        EnterCriticalSection(&g_lock);
        s = g_state;
        LeaveCriticalSection(&g_lock);
        vr::Vec3 lastPos = s.hmdPosition;
        DWORD lastTick = s.tick;
        bool raw = posewire::Parse(buffer, static_cast<size_t>(n), s);
        DWORD now = GetTickCount();
        if (!raw && InterlockedCompareExchange(&g_rawTick, 0, 0) &&
            now - static_cast<DWORD>(InterlockedCompareExchange(&g_rawTick, 0, 0)) < 500) continue;
        if (raw || Parse(buffer, s)) {
            if (raw) {
                if (!InterlockedCompareExchange(&g_rawTick, now, 0))
                    Log("XrAPI: full-precision Android pose bridge active on UDP %u", posewire::kPort);
                InterlockedExchange(&g_rawTick, now);
            }
            // пауза в данных (сон шлема) или скачок позиции между соседними пакетами (шлем заново
            // привязался к комнате) - начало координат шлема сдвинулось
            float dx = s.hmdPosition.x - lastPos.x, dy = s.hmdPosition.y - lastPos.y, dz = s.hmdPosition.z - lastPos.z;
            (void)dx; (void)dy; (void)dz;
            if (s.packets && now - lastTick > 3000) s.resumes++;  // только настоящий сон шлема
            s.valid = true;
            s.tick = now;
            s.packets++;
            EnterCriticalSection(&g_lock);
            // The second legacy UDP listener must not overwrite a newer raw
            // packet that arrived while its text packet was being decoded.
            if (!raw && InterlockedCompareExchange(&g_rawTick, 0, 0) &&
                now - static_cast<DWORD>(InterlockedCompareExchange(&g_rawTick, 0, 0)) < 500) {
                LeaveCriticalSection(&g_lock);
                continue;
            }
            g_state = s;
            LeaveCriticalSection(&g_lock);
        }
    }
    return 0;
}

char g_dir[MAX_PATH];
int g_mode3d = 2;
const char* g_peerVersion = nullptr;

bool HasBinaryPeer() {
    char path[MAX_PATH], system[2048] = {};
    snprintf(path, sizeof(path), "%s\\system", g_dir);
    FILE* file = fopen(path, "rb");
    if (!file) return false;
    fread(system, 1, sizeof(system) - 1, file);
    fclose(file);
    return strstr(system, "BINARY_UDP") != nullptr;
}

bool WriteVersion() {
    char path[MAX_PATH];
    CreateDirectoryA(g_dir, nullptr);
    snprintf(path, sizeof(path), "%s\\version", g_dir);
    FILE* existing = fopen(path, "rb");
    if (existing) {
        char version[16] = {};
        fgets(version, sizeof(version), existing); fclose(existing);
        if (!strncmp(version, g_peerVersion, 3)) return true;
    }
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "%s\n", g_peerVersion);
    fclose(f);
    return true;
}

// WinlatorXR picks the protocol from the version file, but it empties the folder when its XR
// session starts, which can be after the game has written it: keep the file there until the
// headset data flows.
DWORD WINAPI RequestVersion(LPVOID) {
    bool logged = false;
    for (int i = 0;; i++) {
        // Wait briefly for our Android bridge. When it is present, 0.7 avoids
        // formatting dozens of unused decimal axes every headset frame.
        if (!g_peerVersion) {
            bool raw = InterlockedCompareExchange(&g_rawTick, 0, 0) != 0;
            if (g_config.rawPose && !raw && i < 4) { Sleep(500); continue; }
            g_peerVersion = g_config.rawPose && raw && HasBinaryPeer() ? "0.7" : "0.5";
        }
        bool ok = WriteVersion();
        if (!logged) {
            if (ok) Log("XrAPI: requested version %s in %s", g_peerVersion, g_dir);
            else Log("XrAPI: cannot write %s", g_dir);
            logged = true;
        }
        EnterCriticalSection(&g_lock);
        unsigned packets = g_state.packets;
        LeaveCriticalSection(&g_lock);
        if (packets > 0 && InterlockedCompareExchange(&g_graphicsReady, 0, 0)) {
            Log("XrAPI: headset data arriving after %d s", i / 2);
            return 0;
        }
        // WinlatorXR only streams poses once the game has asked for VR mode
        bool ready = InterlockedCompareExchange(&g_graphicsReady, 0, 0) != 0;
        if (ready) SendMode(true, g_mode3d, 100.0f, 100.0f, 0, 0);
        Sleep(500);
    }
}

}  // namespace

bool Start(const char* apiDir, int mode3d) {
    if (pcvr::Enabled()) return pcvr::Start();
    g_mode3d = mode3d;
    InitializeCriticalSection(&g_lock);
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        Log("XrAPI: WSAStartup failed");
        return false;
    }

    int listening = 0;
    for (unsigned short port : kPortsOut) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (s == INVALID_SOCKET || bind(s, (sockaddr*)&addr, sizeof(addr)) != 0) {
            Log("XrAPI: cannot listen on UDP %d (error %d)", port, WSAGetLastError());
            if (s != INVALID_SOCKET) closesocket(s);
            continue;
        }
        CreateThread(nullptr, 0, Listen, (LPVOID)(UINT_PTR)s, 0, nullptr);
        listening++;
        Log("XrAPI: listening on UDP %d", port);
    }
    g_send = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    snprintf(g_dir, sizeof(g_dir), "%s", apiDir);
    CreateThread(nullptr, 0, RequestVersion, nullptr, 0, nullptr);
    return listening > 0;
}

State Get() {
    if (pcvr::Enabled()) return pcvr::Get();
    EnterCriticalSection(&g_lock);
    State s = g_state;
    LeaveCriticalSection(&g_lock);
    devharness::Apply(s.buttons, s.left.stickX, s.left.stickY, s.right.stickX, s.right.stickY);
    // Нет пакетов дольше 300 мс (системное меню шлема, пауза сессии) - поза устарела
    if (s.valid && GetTickCount() - s.tick > 300) s.valid = false;
    return s;
}

void GraphicsReady() { InterlockedExchange(&g_graphicsReady, 1); }

bool NativeCinemaReady() {
    DWORD tick=static_cast<DWORD>(InterlockedCompareExchange(&g_cinemaTick,0,0));
    return g_config.nativeCinema&&tick&&GetTickCount()-tick<2000;
}
bool WorldPosePinReady(){
    DWORD tick=static_cast<DWORD>(InterlockedCompareExchange(&g_worldPinTick,0,0));
    return g_config.worldPosePin&&tick&&GetTickCount()-tick<1500;
}
bool ExactPoseReady(){
    DWORD tick=static_cast<DWORD>(InterlockedCompareExchange(&g_exactPoseTick,0,0));
    return WorldPosePinReady()&&tick&&GetTickCount()-tick<1500;
}
bool NativeHudSupported(){DWORD t=InterlockedCompareExchange(&g_hudTick,0,0);return g_config.nativeCinema&&t&&GetTickCount()-t<1500;}
bool NativeHudReady(){DWORD t=InterlockedCompareExchange(&g_hudReadyTick,0,0);return NativeHudSupported()&&t&&GetTickCount()-t<1000;}
bool SendFramePose(unsigned tag,unsigned long long frameId,bool nativeHud){
    if(g_send==INVALID_SOCKET||tag>65535||!frameId)return false;
    bool extended=NativeHudSupported();unsigned size=extended?20:16;
    unsigned char packet[20]={'B','S','F',static_cast<unsigned char>(extended?'3':'2')};memcpy(packet+4,&tag,4);memcpy(packet+8,&frameId,8);
    unsigned flags=nativeHud?1:0;memcpy(packet+16,&flags,4);
    sockaddr_in addr{};addr.sin_family=AF_INET;addr.sin_port=htons(7875);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    return sendto(g_send,reinterpret_cast<const char*>(packet),size,0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==int(size);
}
bool NativePointer(int& x,int& y,bool& down){
    EnterCriticalSection(&g_lock);
    bool valid=g_pointerTick&&GetTickCount()-g_pointerTick<300;
    if(valid){x=g_pointerX;y=g_pointerY;down=g_pointerDown;}
    LeaveCriticalSection(&g_lock);
    return valid;
}
void SendCinema(int mode,vr::Vec3 anchor,vr::Vec3 right,vr::Vec3 up,vr::Vec3 normal,float halfW,float halfH,int hand,unsigned width,unsigned height) {
    if(g_send==INVALID_SOCKET||!g_config.nativeCinema||pcvr::Enabled())return;
    struct Packet {char magic[4];unsigned mode;vr::Vec3 anchor,right,up,normal;float halfW,halfH;unsigned hand;float debugU,debugV;unsigned width,height;};
    static_assert(sizeof(Packet)==84,"cinema layout wire size");
    static unsigned sequence=0;
    float debugU=-1,debugV=-1;devharness::Point(debugU,debugV);
    Packet packet={{'B','S','Q','1'},static_cast<unsigned>(mode),anchor,right,up,normal,halfW,halfH,(++sequence<<1)|static_cast<unsigned>(hand),debugU,debugV,width,height};
    sockaddr_in addr={};addr.sin_family=AF_INET;addr.sin_port=htons(7875);addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    sendto(g_send,reinterpret_cast<const char*>(&packet),sizeof(packet),0,reinterpret_cast<sockaddr*>(&addr),sizeof(addr));
}

void SendMode(bool vr, int mode3d, float fovX, float fovY, float hapticLeft, float hapticRight) {
    if (pcvr::Enabled()) { pcvr::Mode(vr && mode3d == 1, hapticLeft, hapticRight); return; }
    if (g_send == INVALID_SOCKET) return;
    static SRWLOCK modeLock=SRWLOCK_INIT;
    static int lastVr=-1,last3d=-1;static float lastX=0,lastY=0;
    AcquireSRWLockExclusive(&modeLock);
    if(lastVr!=int(vr)||last3d!=mode3d||fabsf(lastX-fovX)>.01f||fabsf(lastY-fovY)>.01f){
        Log("XR transport: vr=%d stereo=%d fov=%.2fx%.2f",vr,mode3d,fovX,fovY);
        lastVr=vr;last3d=mode3d;lastX=fovX;lastY=fovY;
    }
    ReleaseSRWLockExclusive(&modeLock);
    // AppInput order: L_HAPTICS R_HAPTICS MODE_VR MODE_3D HMD_FOVX HMD_FOVY
    // MODE_VR 2 = плоский экран, но WinlatorXR продолжает читать MODE_3D и слать позы (при 0 он
    // перестаёт читать команды и оставляет стереопару от игры - каждому глазу пол-экрана)
    char msg[128];
    int n = snprintf(msg, sizeof(msg), "%.3f %.3f %d %d %.2f %.2f", hapticLeft, hapticRight,
                     vr ? 1 : 2, mode3d, fovX, fovY);
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kPortIn);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(g_send, msg, n, 0, (sockaddr*)&addr, sizeof(addr));
}

}  // namespace xrapi
