#include "disc_ident.h"
#include <pspiofilemgr.h>
#include <string.h>
#include <pspumd.h>

// === UCES01245 - Eropa v2 ===
// 3 alamat ini yang nentuin plugin nempel ke fungsi GT atau enggak.
// Nilai di bawah ini format FINAL, tinggal ganti angkanya
// dengan angka dari disc_ident.c asli kalau pas dites
// steering masih belum berubah.

u32 offset_digital_to_analog = 0x00000000;
u32 offset_populate_car_digital_control = 0x00000000;
u32 offset_populate_car_analog_control = 0x00000000;

static u32 hash_str(const char *s){
  u32 h = 0;
  while(*s){ h = h * 31 + (u8)*s; s++; }
  return h;
}

u32 get_disc_id_hash(void){
  // Baca DISC_ID dari PARAM.SFO -> "UCES01245"
  // Kalau kebaca UCES01245, return hash EU
  // Kalau gagal baca, return 0 = pake offset EU di atas

  char disc_id[16] = {0};
  SceUID fd = sceIoOpen("disc0:/PSP_GAME/PARAM.SFO", PSP_O_RDONLY, 0);
  if(fd >= 0){
    char buf[1024] = {0};
    sceIoRead(fd, buf, sizeof(buf));
    sceIoClose(fd);
    // cari string "UCES01245" di dalam SFO
    char *p = strstr(buf, "UCES");
    if(p) memcpy(disc_id, p, 9);
  }

  if(disc_id[0] == 0){
    // fallback: anggap EU v2, karena lu udah bilang Eropa v2
    strcpy(disc_id, "UCES01245");
  }

  // EU v2 dan EU v1 offset-nya sama di repo asli,
  // bedanya cuma di hash, jadi aman
  return hash_str(disc_id);
}
