#include <pspkernel.h>
#include <pspctrl.h>

PSP_MODULE_INFO("GTRemastered", 0, 1, 0);
PSP_MAIN_THREAD_NAME("GTRemastered");
PSP_MAIN_THREAD_ATTR(0);

// Forward dari hooking.c
int hooking_init(void);
int hooking_deinit(void);
int digital_to_analog_patched(SceCtrlData *pad);
int populate_car_digital_control_patched(void *car, SceCtrlData *pad);

// ============================================================
// INI YANG KEMARIN HILANG, MAKANYA LINKER ERROR
// ============================================================
int populate_car_analog_control_patched(void *car, SceCtrlData *pad)
{
    if (!pad) return 0;
    // Analog langsung pake yang digital aja, biar nyambung
    return populate_car_digital_control_patched(car, pad);
}

int module_start(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    hooking_init();
    return 0;
}

int module_stop(SceSize args, void *argp)
{
    (void)args;
    (void)argp;
    hooking_deinit();
    return 0;
}
