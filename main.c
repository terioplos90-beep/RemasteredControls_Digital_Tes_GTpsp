#include <pspkernel.h>
#include <pspctrl.h>
#include <pspmodulemgr.h>
#include <pspsysmem.h>
#include <string.h>

// ============================================================
// GTRemastered - hooking.c FULL
// Ini yang dicari exports.exp, jangan dihapus / diganti namanya
// ============================================================

static float wear_test = 0;
static int hooked = 0;

// Alamat fungsi asli, biar bisa manggil yang ori
static void *orig_sceCtrlReadBufferPositive = NULL;

// Forward, biar gak error implicit
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad, int count);
int digital_to_analog_patched(SceCtrlData *pad);
int populate_car_digital_control_patched(void *car, SceCtrlData *pad);

// ============================================================
// 1. digital_to_analog_patched
// Dipanggil sama game buat ubah digital jadi analog
// ============================================================
int digital_to_analog_patched(SceCtrlData *pad)
{
    if (!pad) return 0;

    // Digital -> Analog sederhana, biar mobil tetep jalan
    // Kiri = analog kiri, Kanan = analog kanan
    if (pad->Buttons & PSP_CTRL_LEFT) {
        pad->Lx = 0;
    } else if (pad->Buttons & PSP_CTRL_RIGHT) {
        pad->Lx = 255;
    } else {
        pad->Lx = 128;
    }

    if (pad->Buttons & PSP_CTRL_UP) {
        pad->Ly = 0;
    } else if (pad->Buttons & PSP_CTRL_DOWN) {
        pad->Ly = 255;
    } else {
        pad->Ly = 128;
    }

    return 0;
}

// ============================================================
// 2. populate_car_digital_control_patched
// Ini inti tes wear-nya. Di sini tempat fisika GT nanti
// ============================================================
int populate_car_digital_control_patched(void *car, SceCtrlData *pad)
{
    if (!pad) return 0;

    // --- TES WEAR, UDAH DI DALEM FUNGSI, AMAN ---
    if (pad->Ly < 120) wear_test = wear_test + 1;
    if (pad->Buttons & PSP_CTRL_LEFT) wear_test = wear_test + 1;
    if (pad->Buttons & PSP_CTRL_RIGHT) wear_test = wear_test + 1;
    if (wear_test > 200) pad->Ly = 128; // kalau udah 200, gas dimatiin
    // --- AKHIR TES ---

    // Lanjut ke konversi digital -> analog yang lama
    digital_to_analog_patched(pad);

    // Kalau car-nya ada, jangan diutak-atik dulu, biar gak crash
    (void)car;

    return 0;
}

// ============================================================
// 3. sceCtrlReadBufferPositivePatched
// Ini yang di-hook sama IRSHELL / PRO-CFW
// ============================================================
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad, int count)
{
    int ret = 0;

    // Panggil yang asli dulu kalau ada
    if (orig_sceCtrlReadBufferPositive) {
        // Panggil via function pointer, signature sama kayak sceCtrl
        ret = ((int (*)(SceCtrlData*, int))orig_sceCtrlReadBufferPositive)(pad, count);
    } else {
        // Kalau belum ke-hook, pake API biasa
        ret = sceCtrlReadBufferPositive(pad, count);
    }

    if (ret < 0) return ret;
    if (!pad) return ret;

    // Terapkan kontrol remastered ke pad yang dibaca game
    populate_car_digital_control_patched(NULL, pad);

    return ret;
}

// ============================================================
// Hooking setup - dipanggil dari main.c
// ============================================================
int hooking_init(void)
{
    if (hooked) return 0;
    hooked = 1;

    // Simpan alamat asli, kalau gagal ya pake sceCtrl biasa aja
    // Kita gak pake sctrlHENPatchSyscall di sini biar gak ribet
    // dan gak bikin crash di IRSHELL
    orig_sceCtrlReadBufferPositive = NULL;

    return 0;
}

int hooking_deinit(void)
{
    hooked = 0;
    wear_test = 0;
    return 0;
}
