#include <pspkernel.h>
#include <pspctrl.h>

PSP_MODULE_INFO("GT Test", 0, 1, 0);

static float wear_test = 0;

// Fungsi hook lu yang lama, gw pake nama ini biar nyambung
// Kalau nama fungsi hook lu beda, ganti baris bawah ini doang
int populate_car_analog_control_patched(void *car, SceCtrlData *pad){

  // TES DI SINI, UDAH DI DALEM FUNGSI, AMAN
  if(pad->Ly < 120) wear_test = wear_test + 1;
  if(pad->Buttons & PSP_CTRL_LEFT) wear_test = wear_test + 1;
  if(pad->Buttons & PSP_CTRL_RIGHT) wear_test = wear_test + 1;
  if(wear_test > 200) pad->Ly = 128;

  return 0;
}

int module_start(SceSize args, void *argp){
  return 0;
}
int module_stop(SceSize args, void *argp){
  return 0;
}
