/*
  FULL BUILD - FIX PPSSPP - Menu + Balapan
*/
#include <pspctrl.h>
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <string.h>
#include <pspsdk.h>
#include <pspiofilemgr.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <psputility.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <systemctrl.h>
#include "hooking.h"
#include "disc_ident.h"
#include "logging.h"
#include "common.h"

#define GAME_MODULE_NAME "PDIAPP"
#define MODULE_NAME "GTRemastered"

PSP_MODULE_INFO(MODULE_NAME, PSP_MODULE_KERNEL, 1, 0);

#define EMULATOR_DEVCTL__IS_EMULATOR 0x00000003
#ifndef IRSHELL
static STMOD_HANDLER previous;
#endif
static int is_emulator;
static u32 game_base_addr = 0;

static volatile int btn_x = 0;
static volatile int btn_square = 0;
static volatile int steer_val = 0;

static int (*sceCtrlReadBufferPositiveOrig)(SceCtrlData *pad_data, int count);
static int (*sceCtrlPeekBufferPositiveOrig)(SceCtrlData *pad_data, int count);

static void apply_patch_to_pad(SceCtrlData *pad_data, int count)
{
    int i;
    if (count <= 0) return;
    for (i = 0; i < count; i++) {
        if (btn_x) pad_data[i].Buttons |= PSP_CTRL_CROSS;
        else pad_data[i].Buttons &= ~PSP_CTRL_CROSS;
        if (btn_square) pad_data[i].Buttons |= PSP_CTRL_SQUARE;
        else pad_data[i].Buttons &= ~PSP_CTRL_SQUARE;
        {
            int s = steer_val;
            if (s > 127) s = 127;
            if (s < -127) s = -127;
            pad_data[i].Lx = (unsigned char)(128 + s);
            pad_data[i].Ly = 128;
        }
    }
}

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
    addr.sin_port = 0xB515;
    addr.sin_addr.s_addr = 0;
    if (sceNetInetBind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { sceKernelExitDeleteThread(0); return 0; }
    LOG("N 5557");
    while (1) {
        int len = sceNetInetRecv(sock, buf, sizeof(buf) - 1, 0);
        if (len <= 0) continue;
        buf[len] = 0;
        if (buf[0] == 'S' && buf[1] == '=') {
            int v = 0; int neg = 0; int i = 2;
            if (buf[i] == '-') { neg = 1; i++; }
            while (buf[i] >= '0' && buf[i] <= '9') { v = v * 10 + (buf[i] - '0'); i++; }
            if (neg) v = -v;
            steer_val = v;
        }
        else if (buf[0] == 'X' && buf[1] == '=') {
            btn_x = (buf[2] == '1') ? 1 : 0;
            LOG("BTN X = %s", btn_x ? "ON" : "OFF");
        }
        else if (buf[0] == 'S' && buf[1] == 'Q') {
            int v = 0; int i = 0;
            while (buf[i] && buf[i] != '=') i++;
            if (buf[i] == '=') { i++; v = (buf[i] == '1') ? 1 : 0; }
            btn_square = v;
            LOG("BTN SQUARE = %s", btn_square ? "ON" : "OFF");
        }
    }
    return 0;
}

// ===== FIX PPSSPP: COBA SEMUA NAMA MODUL sceCtrl =====
static void patch_sceCtrl_ppsspp()
{
    const char *mods[] = {
        "sceController_Service",
        "sceCtrl_driver",
        "sceCtrl",
        "sceController",
        NULL
    };
    int i;
    for (i = 0; mods[i] != NULL; i++) {
        void *f1 = (void*)sctrlHENFindFunction((char*)mods[i], (char*)"sceCtrl", 0x1F803938);
        void *f2 = (void*)sctrlHENFindFunction((char*)mods[i], (char*)"sceCtrl", 0x3A622550);
        LOG("try mod %s -> read=0x%08lx peek=0x%08lx", mods[i], (unsigned long)f1, (unsigned long)f2);
        if (f1 && !sceCtrlReadBufferPositiveOrig) {
            sceCtrlReadBufferPositiveOrig = f1;
            sctrlHENPatchSyscall((void*)0x1F803938, (void*)sceCtrlReadBufferPositivePatched);
            LOG("PATCHED sceCtrlRead via %s - MENU OK", mods[i]);
        }
        if (f2 && !sceCtrlPeekBufferPositiveOrig) {
            sceCtrlPeekBufferPositiveOrig = f2;
            sctrlHENPatchSyscall((void*)0x3A622550, (void*)sceCtrlPeekBufferPositivePatched);
            LOG("PATCHED sceCtrlPeek via %s - MENU OK", mods[i]);
        }
        if (sceCtrlReadBufferPositiveOrig && sceCtrlPeekBufferPositiveOrig) break;
    }
    if (!sceCtrlReadBufferPositiveOrig) LOG("FAIL find sceCtrlRead - all mods");
    if (!sceCtrlPeekBufferPositiveOrig) LOG("FAIL find sceCtrlPeek - all mods");
}

int set_offsets(char *disc_id, char *disc_version)
{
    if (strcmp("2.00", disc_version) == 0 && (strcmp("UCES01245", disc_id) == 0 || strcmp("UCUS98632", disc_id) == 0)) {
        offset_digital_to_analog = game_base_addr + 0x14eb40;
        offset_populate_car_digital_control = game_base_addr + 0x126b50;
        offset_populate_car_analog_control = game_base_addr + 0x126dec;
        return 0;
    }
    return -1;
}

static void (*populate_car_analog_control_orig)(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6);
void populate_car_analog_control_patched(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6)
{
    populate_car_analog_control_orig(param_1, param_2, param_3, param_4, param_5, param_6);
}

void digital_to_analog_patched(u32 *param_1, u32 *param_2) { (void)param_1; (void)param_2; }
void populate_car_digital_control_patched(unsigned char *param_1, u32 param_2, u32 param_3) { (void)param_1; (void)param_2; (void)param_3; }

// ============ INIT ============
int init()
{
    char disc_id[50];
    char disc_version[50];
    SceUID thid;
    if (get_disc_id_version(disc_id, disc_version) != 0) { LOG("disc id fail"); return -1; }
    LOG("disc id is %s", disc_id);
    LOG("disc version is %s", disc_version);
    if (set_offsets(disc_id, disc_version) != 0) { LOG("set_offsets fail"); return -1; }

    thid = sceKernelCreateThread("udp_thread", udp_thread, 0x18, 0x10000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);

    patch_sceCtrl_ppsspp();

    HIJACK_FUNCTION(offset_populate_car_analog_control, populate_car_analog_control_patched, populate_car_analog_control_orig);
    LOG("hijacking function at 0x%08lx", offset_populate_car_analog_control);
    LOG("INIT OK - X + SQUARE SIAP - MENU + BALAPAN");
    return 0;
}

#ifndef IRSHELL
int StartPSP(SceModule2 *mod)
{
    char namebuf[sizeof(mod->modname) + 1] = {0};
    memcpy(namebuf, mod->modname, sizeof(mod->modname));
    if (strcmp(namebuf, GAME_MODULE_NAME) == 0) {
        game_base_addr = mod->text_addr;
        game_base_addr = game_base_addr + 0x28;
        init();
    }
    if (!previous) return 0;
    return previous(mod);
}
#else
int irshell_find_module(SceSize args, void *argp)
{
    int cycles = 0;
    while (cycles < 60) {
        SceUID modules[256] = {0};
        SceKernelModuleInfo info = {0};
        int count = 0;
        if (sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0) {
            int i;
            for (i = 0; i < count; i++) {
                info.size = sizeof(SceKernelModuleInfo);
                if (sceKernelQueryModuleInfo(modules[i], &info) < 0) continue;
                char namebuf[sizeof(info.name) + 1] = {0};
                memcpy(namebuf, info.name, sizeof(info.name));
                if (strcmp(namebuf, GAME_MODULE_NAME) == 0) game_base_addr = info.text_addr + 0x28;
            }
            if (game_base_addr != 0) { init(); sceKernelExitDeleteThread(0); return 0; }
        }
        sceKernelDelayThread(1000 * 1000);
        cycles++;
    }
    sceKernelExitDeleteThread(0);
    return 0;
}
#endif

static void StartPPSSPP()
{
    SceUID modules[256];
    SceKernelModuleInfo info;
    int i, count = 0;
    if (sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0) {
        for (i = 0; i < count; ++i) {
            info.size = sizeof(SceKernelModuleInfo);
            if (sceKernelQueryModuleInfo(modules[i], &info) < 0) continue;
            char namebuf[sizeof(info.name) + 1] = {0};
            memcpy(namebuf, info.name, sizeof(info.name));
            if (strcmp(namebuf, GAME_MODULE_NAME) == 0) game_base_addr = info.text_addr;
        }
        if (game_base_addr != 0) init();
    }
}

int module_start(SceSize args, void *argp)
{
#if DEBUG_LOG
    sceIoRemove("ms0:/PSP/GTRemastered.log");
    sceIoRemove("ef0:/PSP/GTRemastered.log");
#endif
    is_emulator = sceIoDevctl("kemulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;
    LOG("module started");
    if (is_emulator) {
        LOG("starting in ppsspp mode");
        StartPPSSPP();
    } else {
#ifndef IRSHELL
        previous = sctrlHENSetStartModuleHandler(StartPSP);
#else
        {
            SceUID thid = sceKernelCreateThread("irshell_find_module", irshell_find_module, 0x18, 0x10000, 0, NULL);
            if (thid < 0) return 0;
            sceKernelStartThread(thid, 0, NULL);
        }
#endif
    }
    return 0;
}

int module_stop(SceSize args, void *argp) { return 0; }
