/*
Remastered Controls: RemasteredControls_GTpsp
Test Build - UDP X + SQUARE only, no tilt, no camera
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

// ================= UDP =================
#define UDP_PORT 5557
#define UDP_BUF_SIZE 128

static volatile int btn_x = 0;
static volatile int btn_square = 0;

static int udp_thread(SceSize args, void *argp)
{
    struct sockaddr_in addr;
    char buf[UDP_BUF_SIZE];
    int sock;
    int res;
    int fromlen;
    int len;

    (void)args;
    (void)argp;

    sock = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        LOG("UDP socket failed: %d", sock);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    res = sceNetInetBind(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (res < 0) {
        LOG("UDP bind failed: %d", res);
        sceNetInetClose(sock);
        sceKernelExitDeleteThread(0);
        return 0;
    }

    LOG("UDP KONEK - LISTEN 5557");

    while (1) {
        fromlen = sizeof(addr);
        len = sceNetInetRecvfrom(sock, buf, UDP_BUF_SIZE - 1, 0, (struct sockaddr *)&addr, (socklen_t *)&fromlen);
        if (len > 0) {
            if (len >= UDP_BUF_SIZE) {
                len = UDP_BUF_SIZE - 1;
            }
            buf[len] = '\0';
            LOG("UDP RX: %s", buf);

            if (strncmp(buf, "X=1", 3) == 0) {
                btn_x = 1;
                LOG("BTN X = ON");
            } else if (strncmp(buf, "X=0", 3) == 0) {
                btn_x = 0;
                LOG("BTN X = OFF");
            } else if (strncmp(buf, "SQUARE=1", 8) == 0) {
                btn_square = 1;
                LOG("BTN SQUARE = ON");
            } else if (strncmp(buf, "SQUARE=0", 8) == 0) {
                btn_square = 0;
                LOG("BTN SQUARE = OFF");
            }
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
    if (thid < 0) {
        LOG("UDP thread create failed: %d", (int)thid);
        return;
    }
    sceKernelStartThread(thid, 0, NULL);
    LOG("UDP THREAD START");
}

// ================= OFFSET =================
int set_offsets(char *disc_id, char *disc_version)
{
    LOG("game_base_addr: 0x%lx", game_base_addr);
    if (strcmp("2.00", disc_version) == 0 && (strcmp("UCES01245", disc_id) == 0 || strcmp("UCUS98632", disc_id) == 0)) {
        offset_populate_car_analog_control = game_base_addr + 0x126dec;
        return 0;
    }
    LOG("unknown disc id %s with version %s", disc_id, disc_version);
    return -1;
}

// ================= HIJACK =================
static void (*populate_car_analog_control_orig)(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6);
void populate_car_analog_control_patched(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6)
{
    short *throttle;
    short *brake;
    int k1;

    (void)param_1;
    (void)param_2;
    (void)param_4;
    (void)param_5;
    (void)param_6;
    (void)populate_car_analog_control_orig;

    throttle = (short *)(&param_3[0x8]);
    brake = (short *)(&param_3[0xA]);

    param_3[0] = 0;
    param_3[1] = 0;

    k1 = pspSdkSetK1(0);

    if (btn_x) {
        param_3[0] |= 2;
        *throttle = 4096;
    }

    if (btn_square) {
        param_3[0] |= 4;
        *brake = 4096;
    }

    pspSdkSetK1(k1);
}

// ================= INIT =================
int init(void)
{
    char disc_id[50];
    char disc_version[50];

    if (get_disc_id_version(disc_id, disc_version) != 0) {
        LOG("cannot find disc id from sfo, aborting");
        return -1;
    }

    LOG("disc id is %s", disc_id);
    LOG("disc version is %s", disc_version);

    if (set_offsets(disc_id, disc_version) != 0) {
        LOG("cannot lookup function offsets, aborting");
        return -1;
    }

    HIJACK_FUNCTION(offset_populate_car_analog_control, populate_car_analog_control_patched, populate_car_analog_control_orig);

    LOG("INIT OK - X + SQUARE SIAP");
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
            if (sceKernelQueryModuleInfo(modules[i], &info) < 0) {
                continue;
            }
            {
                char namebuf[sizeof(info.name) + 1];
                memset(namebuf, 0, sizeof(namebuf));
                memcpy(namebuf, info.name, sizeof(info.name));
                if (strcmp(namebuf, GAME_MODULE_NAME) == 0) {
                    LOG("GTPSP module %s found", namebuf);
                    game_base_addr = info.text_addr;
                }
            }
        }
        if (game_base_addr != 0) {
            init();
        }
    }
}

// ================= MODULE =================
int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;

    LOG("module started");

    is_emulator = sceIoDevctl("kemulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;

    if (is_emulator) {
        LOG("starting in ppsspp mode");
        start_udp();
        StartPPSSPP();
    }

    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    return 0;
}
