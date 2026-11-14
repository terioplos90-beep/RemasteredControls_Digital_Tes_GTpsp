#include <pspkernel.h>
#include <pspctrl.h>
#include <pspnet.h>
#include <pspnet_inet.h>
#include <pspmodulemgr_kernel.h>
#include <string.h>
#include <stdlib.h>

PSP_MODULE_INFO("GTRemastered", PSP_MODULE_USER, 1, 0);

// ===== Variabel UDP =====
volatile int udp_steering = 0;
volatile int udp_accel = 0;
volatile int udp_brake = 0;
volatile int udp_dpad_up = 0;
volatile int udp_has_data = 0;

// ===== 1. Fungsi yang tadi error =====
void patch_pdiapp_imports(void) {
    SceUID ids[64];
    int count = sceKernelGetModuleList(64, ids);
    if (count <= 0) return;

    for (int i = 0; i < count; i++) {
        SceKernelModuleInfo info;
        memset(&info, 0, sizeof(info));
        info.size = sizeof(info);
        if (sceKernelQueryModuleInfo(ids[i], &info) == 0) {
            // Di sini nanti tempat patch PDIAPP
            // Untuk sekarang cuma cek nama modul biar gak error
            if (strcmp(info.name, "PDIAPP") == 0) {
                // ketemu PDIAPP
            }
        }
    }
}

// ===== 2. Fungsi UDP yang tadi error =====
struct MyAddr {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int sin_addr;
    char sin_zero[8];
};

static int udp_thread(SceSize args, void *argp) {
    int sock = sceNetInetSocket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        sceKernelExitDeleteThread(0);
        return -1;
    }

    struct MyAddr addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 0xB515;
    addr.sin_addr = 0;

    if (sceNetInetBind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        sceKernelExitDeleteThread(0);
        return -1;
    }

    char buf[64];
    while (1) {
        struct MyAddr from;
        unsigned long fromlen = sizeof(from);
        int len = sceNetInetRecvfrom(sock, buf, sizeof(buf)-1, 0,
            (struct sockaddr *)&from, (socklen_t *)&fromlen);
        if (len <= 0) continue;
        buf[len] = '\0';

        if (strncmp(buf, "S=", 2) == 0) {
            udp_steering = atoi(buf + 2);
            udp_has_data = 1;
        } else if (strncmp(buf, "X=", 2) == 0) {
            udp_accel = (buf[2] == '1') ? 127 : 0;
            udp_has_data = 1;
        } else if (strncmp(buf, "SQUARE=", 7) == 0) {
            udp_brake = (buf[7] == '1') ? 127 : 0;
            udp_has_data = 1;
        } else if (strncmp(buf, "DPAD_UP=", 8) == 0) {
            udp_dpad_up = (buf[8] == '1') ? 1 : 0;
            udp_has_data = 1;
        }
    }
    return 0;
}

int module_start(SceSize args, void *argp) {
    patch_pdiapp_imports();

    SceUID thid = sceKernelCreateThread("udp_thread", udp_thread, 0x18, 0x10000, 0, NULL);
    if (thid >= 0) {
        sceKernelStartThread(thid, 0, NULL);
    }
    return 0;
}

int module_stop(SceSize args, void *argp) {
    return 0;
}
