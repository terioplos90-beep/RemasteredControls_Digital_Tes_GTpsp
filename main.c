#include <pspkernel.h>
#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspsdk.h>
#include <string.h>
#include <stdio.h>

PSP_MODULE_INFO("PDIAPP_PAD", 0x1000, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

// ============ LOG ============
#define LOG(...) do { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
    pspDebugScreenPrintf("%s\n", _buf); \
} while(0)

// ============ GLOBAL STATE ============
// Diubah dari UDP / HP
static volatile int btn_x_state = 0;
static volatile int btn_square_state = 0;
static volatile int steer_val = 0; // -127 sampai 127, 0 = tengah

// Original function pointers
typedef int (*SceCtrlReadFunc)(SceCtrlData *pad_data, int count);
static SceCtrlReadFunc sceCtrlReadBufferPositiveOrig = NULL;
static SceCtrlReadFunc sceCtrlPeekBufferPositiveOrig = NULL;

// ============ APPLY PATCH KE PAD ============
static void apply_patch_to_pad(SceCtrlData *pad_data, int count)
{
    int i;
    if (!pad_data || count <= 0) return;

    for (i = 0; i < count; i++) {
        // Tombol: OR, jangan overwrite!
        if (btn_x_state) pad_data[i].Buttons |= PSP_CTRL_CROSS;
        if (btn_square_state) pad_data[i].Buttons |= PSP_CTRL_SQUARE;

        // Analog: cuma diubah kalau lagi belok, biar menu gak ke-lock
        if (steer_val != 0) {
            int s = steer_val;
            if (s > 127) s = 127;
            if (s < -127) s = -127;
            pad_data[i].Lx = (unsigned char)(128 + s);
            pad_data[i].Ly = 128;
        }
    }
}

// ============ PATCHED FUNCTIONS ============
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count)
{
    int ret;
    int k1 = pspSdkSetK1(0);
    ret = sceCtrlReadBufferPositiveOrig(pad_data, count);
    if (ret > 0) apply_patch_to_pad(pad_data, ret);
    pspSdkSetK1(k1);
    return ret;
}

int sceCtrlPeekBufferPositivePatched(SceCtrlData *pad_data, int count)
{
    int ret;
    int k1 = pspSdkSetK1(0);
    ret = sceCtrlPeekBufferPositiveOrig(pad_data, count);
    if (ret > 0) apply_patch_to_pad(pad_data, ret);
    pspSdkSetK1(k1);
    return ret;
}

// ============ PATCH IMPORTS SEMUA MODUL ============
static void patch_one_module_imports(SceModule *mod)
{
    u32 *stub, *end;
    if (!mod) return;

    stub = (u32*)mod->stub_top;
    end = (u32*)(mod->stub_top + mod->stub_size);

    for (; stub < end; ) {
        u32 len_v = stub[2];
        u16 stubcount = (u16)((len_v >> 16) & 0xFFFF);
        u16 slen = (u16)(len_v & 0xFFFF);
        u32 nidtable = stub[3];
        u32 stubtable = stub[4];
        u32 *nids = (u32*)nidtable;
        u32 *stubs = (u32*)stubtable;
        int j;

        for (j = 0; j < stubcount; j++) {
            if (nids[j] == 0x1F803938) { // sceCtrlReadBufferPositive
                if (!sceCtrlReadBufferPositiveOrig)
                    sceCtrlReadBufferPositiveOrig = (SceCtrlReadFunc)stubs[j];
                stubs[j] = (u32)sceCtrlReadBufferPositivePatched;
                LOG("PATCHED Read in %s", mod->modname);
            }
            if (nids[j] == 0x3A622550) { // sceCtrlPeekBufferPositive
                if (!sceCtrlPeekBufferPositiveOrig)
                    sceCtrlPeekBufferPositiveOrig = (SceCtrlReadFunc)stubs[j];
                stubs[j] = (u32)sceCtrlPeekBufferPositivePatched;
                LOG("PATCHED Peek in %s", mod->modname);
            }
        }

        stub += 5;
        if (slen > 5) stub += (slen - 5);
    }
}

static void patch_pdiapp_imports()
{
    SceUID ids[64];
    int count = 0;
    int i;

    if (sceKernelGetModuleList(0, 64, ids, &count) == 0) {
        for (i = 0; i < count; i++) {
            SceModule *mod = (SceModule*)sceKernelFindModuleByUID(ids[i]);
            if (mod) patch_one_module_imports(mod);
        }
    } else {
        SceModule *mod = (SceModule*)sceKernelFindModuleByName("PDIAPP");
        patch_one_module_imports(mod);
    }

    if (!sceCtrlReadBufferPositiveOrig) LOG("FAIL IMPORT Read - not found");
    if (!sceCtrlPeekBufferPositiveOrig) LOG("FAIL IMPORT Peek - not found");
}

// ============ UDP THREAD ============
// Protokol dari HP, kirim string gini:
// "X:1" = X ON, "X:0" = X OFF
// "S:1" = SQUARE ON, "S:0" = SQUARE OFF
// "ST:-50" = setir ke -50
static int udp_thread(SceSize args, void *argp)
{
    int sock;
    struct sockaddr_in addr;
    char buf[128];

    LOG("UDP THREAD START");
    sock = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { sceKernelExitDeleteThread(0); return 0; }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0xB515; // Port 55555, byte swap: 0xB515
    addr.sin_addr.s_addr = 0;

    if (sceNetInetBind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        LOG("UDP BIND FAIL");
        sceKernelExitDeleteThread(0);
        return 0;
    }

    LOG("UDP LISTEN 55555");

    while (1) {
        int len = sceNetInetRecv(sock, buf, sizeof(buf) - 1, 0);
        if (len > 0) {
            buf[len] = 0;
            // X
            if (strncmp(buf, "X:1", 3) == 0) { btn_x_state = 1; LOG("BTN X = ON"); }
            else if (strncmp(buf, "X:0", 3) == 0) { btn_x_state = 0; LOG("BTN X = OFF"); }
            // SQUARE
            else if (strncmp(buf, "S:1", 3) == 0) { btn_square_state = 1; LOG("BTN SQUARE = ON"); }
            else if (strncmp(buf, "S:0", 3) == 0) { btn_square_state = 0; LOG("BTN SQUARE = OFF"); }
            // STEER
            else if (strncmp(buf, "ST:", 3) == 0) {
                int v = 0;
                sscanf(buf + 3, "%d", &v);
                steer_val = v;
            }
        }
        sceKernelDelayThread(1000);
    }
    return 0;
}

// ============ MAIN ============
int module_start(SceSize args, void *argp)
{
    SceUID thid;

    pspDebugScreenInit();
    LOG("module started");
    LOG("starting in ppsspp mode");

    // Patch dulu biar dari menu udah jalan
    patch_pdiapp_imports();

    thid = sceKernelCreateThread("udp_thread", udp_thread, 0x12, 0x10000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);

    LOG("INIT OK - X + SQUARE SIAP - MENU + BALAPAN");
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    return 0;
}
