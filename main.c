#include <pspkernel.h>
#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspsysmem.h>
#include <pspmodinfo.h>
#include <pspsystemctrl_kernel.h>
#include <string.h>

PSP_MODULE_INFO("GTRemastered", PSP_MODULE_KERNEL, 1, 1);
PSP_MAIN_THREAD_ATTR(0);
PSP_MAIN_THREAD_STACK_SIZE_KB(64);

#define IS_EMULATOR 0x00000000

int _atoi(const char *s) {
    int neg = 0; int val = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0'); s++;
    }
    if (neg) val = val * (-1);
    return val;
}

int (*sceCtrlReadBufferPositive_orig)(SceCtrlData *pad, int count) = NULL;
static int enabled = 1;

void digital_to_analog_patched() { return; }
void populate_car_digital_control_patched() { return; }

int sceCtrlReadBufferPositivePatched(SceCtrlData *pad, int count) {
    int ret;
    if (sceCtrlReadBufferPositive_orig) {
        ret = sceCtrlReadBufferPositive_orig(pad, count);
    } else {
        ret = sceCtrlReadBufferPositive(pad, count);
    }
    if (!enabled || ret <= 0) return ret;
    digital_to_analog_patched();
    populate_car_digital_control_patched();
    return ret;
}

static SceModuleInfo *previous = NULL;
int StartPSP(SceModuleInfo *mod) {
    if (!previous) return 0;
    return previous(mod);
}

int module_start(SceSize args, void *argp) {
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    return 0;
}
int module_stop(SceSize args, void *argp) { return 0; }
