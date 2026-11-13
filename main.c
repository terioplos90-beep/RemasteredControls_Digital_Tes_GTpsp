/*
FIX FINAL: build PPSSPP + IRSHELL, tanpa atoi, tanpa unused thid
*/

#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspkernel.h>
#include <netinet/in.h>
#include <string.h>
#include "hooking.h"
#include "disc_ident.h"
#include "logging.h"
#include <systemctrl.h>
#include "common.h"

#define GAME_MODULE_NAME "PDIAPP"
PSP_MODULE_INFO(MODULE_NAME, PSP_MODULE_KERNEL, 1, 0);
#define EMULATOR_DEVCTL__IS_EMULATOR 0x00000003

static int is_emulator;
static u32 game_base_addr = 0;

#define UDP_PORT 5557
#define UDP_BUF_SIZE 128
static volatile int btn_x = 0;
static volatile int btn_square = 0;
static volatile int steer_val = 0;

// --- PENGGANTI atoi ---
static int parse_int(const char *s)
{
    int sign = 1;
    int v = 0;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

static int udp_thread(SceSize args, void *argp)
{
    struct sockaddr_in addr;
    char buf[UDP_BUF_SIZE];
    int sock, res, fromlen, len;
    (void)args; (void)argp;

    sock = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { sceKernelExitDeleteThread(0); return 0; }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    res = sceNetInetBind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (res < 0) { sceNetInetClose(sock); sceKernelExitDeleteThread(0); return 0; }
    LOG("UDP KONEK - LISTEN 5557");

    while (1) {
        fromlen = sizeof(addr);
        len = sceNetInetRecvfrom(sock, buf, UDP_BUF_SIZE - 1, 0, (struct sockaddr *)&addr, (socklen_t *)&fromlen);
        if (len > 0) {
            if (len >= UDP_BUF_SIZE) len = UDP_BUF_SIZE - 1;
            buf[len] = '\0';
            if (strncmp(buf, "X=1", 3) == 0) { btn_x = 1; LOG("BTN X = ON"); }
            else if (strncmp(buf, "X=0", 3) == 0) { btn_x = 0; LOG("BTN X = OFF"); }
            else if (strncmp(buf, "SQUARE=1", 8) == 0) { btn_square = 1; LOG("BTN SQUARE = ON"); }
            else if (strncmp(buf, "SQUARE=0", 8) == 0) { btn_square = 0; LOG("BTN SQUARE = OFF"); }
            else if (strncmp(buf, "S=", 2) == 0) { steer_val = parse_int(&buf[2]); }
        }
    }
    return 0;
}

static void start_udp(void)
{
    SceUID thid;
    sceNetInit(128 * 1024, 0x20, 0x1000, 0x20, 0x1000);
    sceNetInetInit();
    thid = sceKernelCreateThread("UDP2BTN", udp_thread, 0x18, 0x4000, 0, NULL);
    if (thid >= 0) sceKernelStartThread(thid, 0, NULL);
    LOG("UDP THREAD START");
}

int set_offsets(char *disc_id, char *disc_version)
{
    LOG("game_base_addr: 0x%lx", game_base_addr);
    if (strcmp("2.00", disc_version) == 0 && (strcmp("UCES01245", disc_id) == 0 || strcmp("UCUS98632", disc_id) == 0)) {
        offset_digital_to_analog = game_base_addr + 0x14eb40;
        offset_populate_car_digital_control = game_base_addr + 0x126b50;
        offset_populate_car_analog_control = game_base_addr + 0x126dec;
        return 0;
    }
    LOG("unknown disc id %s with version %s", disc_id, disc_version);
    return -1;
}

static int (*sceCtrlReadBufferPositiveOrig)(SceCtrlData *pad_data, int count);
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count)
{
    return sceCtrlReadBufferPositiveOrig(pad_data, count);
}

static void (*digital_to_analog_orig)(u32 *param_1, u32 *param_2);
void digital_to_analog_patched(u32 *param_1, u32 *param_2)
{
    if (digital_to_analog_orig) digital_to_analog_orig(param_1, param_2);
}

static void (*populate_car_digital_control_orig)(unsigned char *param_1, u32 param_2, u32 param_3);
void populate_car_digital_control_patched(unsigned char *param_1, u32 param_2, u32 param_3)
{
    if (populate_car_digital_control_orig) {
        populate_car_digital_control_orig(param_1, param_2, param_3);
    }
    if (btn_x) {
        param_1[0] |= 2;
        *(short *)(&param_1[0x8]) = 4096;
    }
    if (btn_square) {
        param_1[0] |= 4;
        *(short *)(&param_1[0xA]) = 4096;
    }
    {
        int s = steer_val;
        if (s > 127) s = 127;
        if (s < -127) s = -127;
        *(short *)(&param_1[0xC]) = (short)(s * 32);
        if (s != 0) param_1[0] |= 1;
    }
}

static void (*populate_car_analog_control_orig)(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6);
void populate_car_analog_control_patched(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6)
{
    short *throttle;
    short *brake;
    short *steer;
    int k1;
    (void)param_1; (void)param_2; (void)param_4; (void)param_5; (void)param_6;
    (void)populate_car_analog_control_orig;
    throttle = (short *)(&param_3[0x8]);
    brake = (short *)(&param_3[0xA]);
    steer = (short *)(&param_3[0xC]);
    param_3[0] = 0;
    param_3[1] = 0;
    k1 = pspSdkSetK1(0);
    if (btn_x) { param_3[0] |= 2; *throttle = 4096; }
    if (btn_square) { param_3[0] |= 4; *brake = 4096; }
    {
        int s = steer_val;
        if (s > 127) s = 127;
        if (s < -127) s = -127;
        *steer = (short)(s * 32);
        if (s != 0) param_3[0] |= 1;
    }
    pspSdkSetK1(k1);
}

int init(void)
{
    char disc_id[50];
    char disc_version[50];
    if (get_disc_id_version(disc_id, disc_version) != 0) return -1;
    LOG("disc id is %s", disc_id);
    LOG("disc version is %s", disc_version);
    if (set_offsets(disc_id, disc_version) != 0) return -1;
    HIJACK_FUNCTION(offset_populate_car_digital_control, populate_car_digital_control_patched, populate_car_digital_control_orig);
    HIJACK_FUNCTION(offset_populate_car_analog_control, populate_car_analog_control_patched, populate_car_analog_control_orig);
    LOG("INIT OK - DIGITAL + ANALOG HIJACK");
    return 0;
}

static void StartPPSSPP(void)
{
    SceUID modules[256];
    SceKernelModuleInfo info;
    int i;
    int count = 0;
    if (sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0) {
        for (i = 0; i < count; ++i) {
            info.size = sizeof(SceKernelModuleInfo);
            if (sceKernelQueryModuleInfo(modules[i], &info) < 0) continue;
            {
                char namebuf[sizeof(info.name) + 1];
                memset(namebuf, 0, sizeof(namebuf));
                memcpy(namebuf, info.name, sizeof(info.name));
                if (strcmp(namebuf, GAME_MODULE_NAME) == 0) game_base_addr = info.text_addr;
            }
        }
        if (game_base_addr != 0) init();
    }
}

// IRSHELL: selalu ada, biar ga error pas -DIRSHELL=1 atau engga
int irshell_find_module(SceSize args, void *argp)
{
    (void)args; (void)argp;
    LOG("IRSHELL find module mulai");
    while (1) {
        StartPPSSPP();
        if (game_base_addr != 0) break;
        sceKernelDelayThread(1000 * 1000);
    }
    sceKernelExitDeleteThread(0);
    return 0;
}

int module_start(SceSize args, void *argp)
{
    (void)args; (void)argp;
    LOG("module started");
    is_emulator = sceIoDevctl("kemulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;
    LOG("is_emulator=%d", is_emulator);
    start_udp();

    if (is_emulator) {
        LOG("starting in ppsspp mode");
        StartPPSSPP();
    } else {
        SceUID thid2;
        LOG("starting in irshell mode");
        thid2 = sceKernelCreateThread("irshell_find", irshell_find_module, 0x18, 0x10000, 0, NULL);
        if (thid2 >= 0) sceKernelStartThread(thid2, 0, NULL);
    }
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args; (void)argp;
    return 0;
}
