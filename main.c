#include <pspctrl.h>
#include <string.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include "hooking.h"
#include "disc_ident.h"
#include "logging.h"
#include <systemctrl.h>
#include "common.h"
#define GAME_MODULE_NAME "PDIAPP"
PSP_MODULE_INFO(MODULE_NAME, PSP_MODULE_KERNEL, 1, 0);
#define EMULATOR_DEVCTL__IS_EMULATOR 0x00000003
#ifndef IRSHELL
static STMOD_HANDLER previous;
#endif
static int is_emulator;
static u32 game_base_addr = 0;
static int override_accel = 0;
static int accel_override = 0;
static int override_brake = 0;
static int brake_override = 0;
static int override_steering = 0;
static short int steering_override = 0;
static int override_camera = 0;
static float camera_override = 0;
static unsigned char outer_deadzone = 114;
static unsigned char inner_deadzone = 10;
static int camera_controls = 0;
static int adjacent_axes = 0;
typedef struct { float wear; float temp; float grip; int stage; int is_flat; } TireGT4;
static TireGT4 tireFL, tireFR, tireRL, tireRR;
static float total_km_gt4 = 0.0f;
static int pit_count_gt4 = 0;
static int gt4_initialized = 0;
static float last_speed_gt4 = 0.0f;
static float last_steer_gt4 = 0.0f;
static int gt4_counter = 0;
static int gt4_hud_on = 1;
#define GT4_BAN_HABIS_KM_SOFT 7.0f
#define GT4_GRIP_AWAL_SOFT 1.05f
static float gt4_absf(float v){ if(v < 0.0f) return -v; return v; }
static float gt4_clampf(float v, float mn, float mx){ if(v < mn) return mn; if(v > mx) return mx; return v; }
static void gt4_reset_all(){
    tireFL.wear = 0; tireFL.temp = 50.0f; tireFL.grip = GT4_GRIP_AWAL_SOFT; tireFL.stage = 0; tireFL.is_flat = 0;
    tireFR.wear = 0; tireFR.temp = 50.0f; tireFR.grip = GT4_GRIP_AWAL_SOFT; tireFR.stage = 0; tireFR.is_flat = 0;
    tireRL.wear = 0; tireRL.temp = 50.0f; tireRL.grip = GT4_GRIP_AWAL_SOFT; tireRL.stage = 0; tireRL.is_flat = 0;
    tireRR.wear = 0; tireRR.temp = 50.0f; tireRR.grip = GT4_GRIP_AWAL_SOFT; tireRR.stage = 0; tireRR.is_flat = 0;
    total_km_gt4 = 0; gt4_initialized = 1;
}
static float gt4_calc_grip(TireGT4 *t){
    if(t->is_flat) return 0.30f;
    if(t->wear < 30.0f) t->stage = 0;
    else if(t->wear < 65.0f) t->stage = 1;
    else if(t->wear < 85.0f) t->stage = 2;
    else t->stage = 3;
    float g = 1.05f - (t->wear / 100.0f) * 0.60f;
    if(t->temp > 125.0f) g -= 0.15f;
    if(t->temp < 50.0f) g -= 0.05f;
    return gt4_clampf(g, 0.30f, 1.05f);
}
static unsigned int __attribute__((aligned(16))) gu_list[262144];
static int gu_initialized = 0;
typedef struct { float x, y, z; unsigned int color; } GuVertex;
static void gu_rect(float x, float y, float w, float h, unsigned int color){
    GuVertex* v = (GuVertex*)sceGuGetMemory(sizeof(GuVertex) * 2);
    v[0].x = x; v[0].y = y; v[0].z = 0; v[0].color = color;
    v[1].x = x + w; v[1].y = y + h; v[1].z = 0; v[1].color = color;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
}
static void gt4_draw_hud_gu(){
    if(!gt4_hud_on) return; if(!gt4_initialized) return;
    if(!gu_initialized){ sceGuInit(); gu_initialized = 1; }
    sceGuStart(GU_DIRECT, gu_list);
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_TEXTURE_2D);
    sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    TireGT4* list[4] = {&tireFL, &tireFR, &tireRL, &tireRR};
    float HUD_X = 180; float HUD_Y = 38;
    gu_rect(HUD_X-4, HUD_Y-4, 122, 54, 0xCC000000);
    gu_rect(HUD_X-4, HUD_Y-4, 122, 2, 0xFFFFFFFF);
    gu_rect(HUD_X-4, HUD_Y+48, 122, 2, 0xFFFFFFFF);
    for(int i=0;i<4;i++){
        float y = HUD_Y + i * 11;
        int remain = 100 - (int)list[i]->wear; if(remain < 0) remain = 0;
        float bar_w = (remain * 80) / 100.0f;
        unsigned int col = 0xFF00FF00;
        if(list[i]->is_flat) col = 0xFF0000FF;
        else if(list[i]->stage == 1) col = 0xFF00FFFF;
        else if(list[i]->stage == 2) col = 0xFF0080FF;
        else if(list[i]->stage == 3) col = 0xFF0000FF;
        gu_rect(HUD_X, y, 18, 8, 0xFFFFFFFF);
        gu_rect(HUD_X+1, y+1, 16, 6, col);
        gu_rect(HUD_X+22, y, 82, 8, 0xFF3C3C3C);
        if(bar_w > 0) gu_rect(HUD_X+22, y, bar_w, 8, col);
        if(list[i]->is_flat && ((gt4_counter % 60) < 30)) gu_rect(HUD_X+22, y, 82, 8, 0xFF0000FF);
    }
    sceGuFinish(); sceGuSync(0, 0);
}
static int gt4_hud_thread(SceSize args, void *argp){
    LOG("GT4 HUD thread started - GU MODE");
    while(1){ sceDisplayWaitVblankStart(); gt4_draw_hud_gu(); sceKernelDelayThread(16666); }
    return 0;
}
static void gt4_update(float steering_norm, float throttle_norm, float brake_norm, float dt){
    if(!gt4_initialized) gt4_reset_all();
    gt4_counter++;
    float target = throttle_norm * 280.0f;
    if(brake_norm > 0.1f){ last_speed_gt4 -= brake_norm * 180.0f * dt * 10.0f; if(last_speed_gt4 < target) last_speed_gt4 = target; }
    else { last_speed_gt4 += (target - last_speed_gt4) * dt * 0.5f; }
    if(last_speed_gt4 < 0.0f) last_speed_gt4 = 0.0f; if(last_speed_gt4 > 320.0f) last_speed_gt4 = 320.0f;
    float speed = last_speed_gt4;
    float jarak_meter = (speed / 3.6f) * dt; total_km_gt4 += jarak_meter / 1000.0f;
    float slip = gt4_absf(steering_norm) * (speed / 180.0f);
    float steer_delta = gt4_absf(steering_norm - last_steer_gt4) / dt;
    int is_braking_hard = (brake_norm > 0.7f && speed > 80.0f);
    int is_drifting = (slip > 0.7f && speed > 80.0f);
    int is_wheelspin = (throttle_norm > 0.9f && speed < 120.0f);
    float beban_kiri = 1.0f, beban_kanan = 1.0f;
    if(steering_norm > 0.2f){ beban_kiri = 1.0f + steering_norm * 1.4f; beban_kanan = 1.0f - steering_norm * 0.3f; }
    else if(steering_norm < -0.2f){ beban_kanan = 1.0f + gt4_absf(steering_norm) * 1.4f; beban_kiri = 1.0f - gt4_absf(steering_norm) * 0.3f; }
    TireGT4* list[4] = {&tireFL, &tireFR, &tireRL, &tireRR};
    float beban[4] = {beban_kiri, beban_kanan, beban_kiri*0.85f, beban_kanan*0.85f};
    int is_front[4] = {1,1,0,0};
    for(int i=0;i<4;i++){
        TireGT4 *t = list[i]; if(t->is_flat) continue;
        float heat = (speed/300.0f)*7.0f*dt + slip*22.0f*dt + steer_delta*0.002f*dt;
        if(is_front[i]) heat *= 1.35f;
        t->temp += heat - (2.8f*dt); t->temp = gt4_clampf(t->temp, 20.0f, 160.0f);
        float w = (jarak_meter / (GT4_BAN_HABIS_KM_SOFT * 1000.0f)) * 100.0f * 3.0f;
        w *= beban[i];
        if(slip > 0.2f) w *= (1.0f + slip * 1.5f);
        if(steer_delta > 2.0f && is_front[i]) w *= 1.8f;
        if(t->temp > 130.0f) w *= 2.0f;
        if(t->temp < 55.0f && speed > 120.0f) w *= 1.5f;
        if(is_front[i]) w *= 1.20f; else w *= 1.60f;
        t->wear += w; t->wear = gt4_clampf(t->wear, 0.0f, 100.0f);
        if(t->wear >= 98.0f && t->temp > 125.0f){ if((gt4_counter % 2000) == 0) t->is_flat = 1; }
        t->grip = gt4_calc_grip(t);
    }
    if(is_braking_hard){ tireFL.wear += 2.0f * dt; tireFR.wear += 2.0f * dt; }
    if(is_drifting){ tireRL.wear += 3.0f * dt; tireRR.wear += 3.0f * dt; }
    if(is_wheelspin){ tireRL.wear += 2.0f * dt; tireRR.wear += 2.0f * dt; }
    last_steer_gt4 = steering_norm;
    static int lc = 0; lc++; if(lc >= 300){ lc = 0; LOG("GT4 TIRE | FL:%d FR:%d RL:%d RR:%d | KM:%d Pit:%d", (int)tireFL.wear, (int)tireFR.wear, (int)tireRL.wear, (int)tireRR.wear, (int)total_km_gt4, pit_count_gt4); }
}
static float gt4_front_grip(){ return (tireFL.grip + tireFR.grip) / 2.0f; }
static float gt4_rear_grip(){ return (tireRL.grip + tireRR.grip) / 2.0f; }
static int apply_deadzone(int val){ if(val < inner_deadzone){ return 0; } int range = outer_deadzone - inner_deadzone; val = val - inner_deadzone; if(val > range){ val = range; } return val * 127 / range; }
static void sample_input(SceCtrlData *pad_data, int count, int negative){
    if(count < 1){ return; }
    int rx = pad_data[count - 1].Rsrv[0]; int ry = pad_data[count - 1].Rsrv[1];
    int lx = pad_data[count - 1].Lx; int ly = pad_data[count - 1].Ly;
    int lxp = 0; int lxn = 0; int lyp = 0; int lyn = 0; int rxn = 0; int ryp = 0; int ryn = 0;
    static int right_stick_looks_dead = 1;
    if(right_stick_looks_dead && (rx != 0 || ry != 0)){ right_stick_looks_dead = 0; }
    if(lx < 128){ lxn = apply_deadzone(128 - lx); } if(lx > 128){ lxp = apply_deadzone(lx - 128); }
    if(ly < 128){ lyn = apply_deadzone(128 - ly); } if(ly > 128){ lyp = apply_deadzone(ly - 128); }
    if(rx < 128){ rxn = apply_deadzone(128 - rx); } if(ry < 128){ ryn = apply_deadzone(128 - ry); } if(ry > 128){ ryp = apply_deadzone(ry - 128); }
    if(adjacent_axes){ int tmp = ryn; ryn = rxn; rxn = tmp; }
    override_brake = 0; override_accel = 0; override_steering = 0; override_camera = 0;
    if(lxp > 0){ override_steering = 1; steering_override = -lxp; }
    if(lxn > 0){ override_steering = 1; steering_override = lxn; }
    u32 btn = pad_data[count - 1].Buttons;
    if(btn & PSP_CTRL_CROSS){ override_accel = 1; accel_override = 127; }
    if(btn & PSP_CTRL_SQUARE){ override_brake = 1; brake_override = 127; }
    if(!right_stick_looks_dead){
        if(!override_brake && ryp > 0){ override_brake = 1; brake_override = ryp; }
        if(!override_accel && ryn > 0){ override_accel = 1; accel_override = ryn; }
    }
    if(!override_steering){
        if(btn & PSP_CTRL_LEFT){ override_steering = 1; steering_override = 127; }
        if(btn & PSP_CTRL_RIGHT){ override_steering = 1; steering_override = -127; }
    }
    if(camera_controls){
        if(lyn > 0){ override_camera = 1; camera_override = (float)(lyn * -1.5f) / 127.0f; }
        if(lyp > 0){ override_camera = 1; camera_override = (float)(lyp * 1.5f) / 127.0f; }
    }
}
static int (*sceCtrlReadBufferPositiveOrig)(SceCtrlData *pad_data, int count);
int sceCtrlReadBufferPositivePatched(SceCtrlData *pad_data, int count){
    int k1 = pspSdkSetK1(0); int res = sceCtrlReadBufferPositiveOrig(pad_data, count); sample_input(pad_data, res, 0); pspSdkSetK1(k1); return res;
}
int set_offsets(char *disc_id, char *disc_version){
    if(strcmp("2.00", disc_version) == 0 && (strcmp("UCES01245", disc_id) == 0 || strcmp("UCUS98632", disc_id) == 0)){
        offset_digital_to_analog = game_base_addr + 0x14eb40;
        offset_populate_car_digital_control = game_base_addr + 0x126b50;
        offset_populate_car_analog_control = game_base_addr + 0x126dec; return 0;
    }
    if(strcmp("1.00", disc_version) == 0 && strcmp("UCAS40265", disc_id) == 0){ offset_populate_car_analog_control = game_base_addr + 0x126dec; return 0; }
    if(strcmp("1.01", disc_version) == 0 && strcmp("UCJS10100", disc_id) == 0){ offset_populate_car_analog_control = game_base_addr + 0x126dd0; return 0; }
    return -1;
}
static void (*digital_to_analog_orig)(u32 *param_1, u32 *param_2);
void digital_to_analog_patched(u32 *param_1, u32 *param_2){
    float *accel = (float *)((u32)(param_1[2]) + 0x530); u32 *accel_as_int = (u32 *)accel;
    float *brake = (float *)((u32)(param_1[2]) + 0x538); u32 *brake_as_int = (u32 *)brake;
    digital_to_analog_orig(param_1, param_2);
    if(*accel_as_int != 0 && override_accel){ *accel = (float)accel_override / 127.0f; if(*accel > 1.0f){ *accel = 1.0f; } }
    if(*brake_as_int != 0 && override_brake){ *brake = (float)brake_override / 127.0f; if(*brake > 1.0f){ *brake = 1.0f; } }
}
static void (*populate_car_digital_control_orig)(unsigned char *param_1, u32 param_2, u32 param_3);
void populate_car_digital_control_patched(unsigned char *param_1, u32 param_2, u32 param_3){
    unsigned short int *accel_control = (unsigned short int *)&param_1[8]; unsigned short int *brake_control = (unsigned short int *)&param_1[10];
    populate_car_digital_control_orig(param_1, param_2, param_3);
    if(override_accel){ *accel_control = 1; param_1[0] &= 0x9d; }
    if(override_brake){ *brake_control = 1; param_1[0] &= 0xfb; }
}
static void (*populate_car_analog_control_orig)(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6);
void populate_car_analog_control_patched(u32 param_1, int *param_2, unsigned char *param_3, u32 param_4, u32 param_5, unsigned char param_6){
    short *steering = (short *)(&param_3[4]); float *camera_rotation = (float *)(&param_3[0x2c]); short *throttle = (short *)(&param_3[0x8]); short *brake = (short *)(&param_3[0xA]);
    param_3[0] = 0; param_3[1] = 0; int k1 = pspSdkSetK1(0);
    int mode; sceCtrlGetSamplingMode(&mode); if(mode != PSP_CTRL_MODE_ANALOG){ sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG); return; }
    { SceCtrlData pad_data; int res = sceCtrlPeekBufferPositive(&pad_data, 1); sample_input(&pad_data, res, 0);
      if((pad_data.Buttons & PSP_CTRL_SELECT) && (pad_data.Buttons & PSP_CTRL_START)){ gt4_reset_all(); pit_count_gt4++; sceKernelDelayThread(500000); } }
    float steer_norm2 = (float)steering_override / 127.0f; float thr_norm2 = override_accel ? (float)accel_override / 127.0f : 0.0f; float brk_norm2 = override_brake ? (float)brake_override / 127.0f : 0.0f;
    if(!override_steering) steer_norm2 = 0.0f;
    gt4_update(steer_norm2, thr_norm2, brk_norm2, 1.0f/60.0f);
    float grip_f = gt4_front_grip(); float grip_r = gt4_rear_grip();
    if(override_steering){ param_3[0] = param_3[0] | 1; param_3[1] = param_3[1] | 2; float steer_final = (float)steering_override;
        if(grip_f < 0.90f){ steer_final *= (0.60f + grip_f * 0.40f); }
        if(grip_r < 0.80f){ static int ph = 0; ph += 7; int tri = (ph % 100) - 50; if(tri < 0) tri = -tri; float wobble = (float)(tri - 25) * (0.80f - grip_r) * 0.8f; steer_final += wobble; steer_final *= (1.0f + (0.80f - grip_r) * 0.5f); }
        if(steer_final > 127.0f) steer_final = 127.0f; if(steer_final < -127.0f) steer_final = -127.0f;
        *steering = (short)(steer_final * 0x2000 / 127); }
    else{ *steering = 0; }
    if(override_accel){ param_3[0] = param_3[0] | 2; int thr_eff = accel_override;
        if(accel_override >= 120){ *throttle = 4096; if(grip_r < 0.90f){ *throttle = (short)(4096 * (0.60f + grip_r * 0.40f)); } }
        else { if(thr_eff >= 95){ *throttle = 3932 + (thr_eff-95)*5; } else if(thr_eff >= 64){ *throttle = 3727 + (thr_eff-64)*6; } else if(thr_eff >= 32){ *throttle = 3317 + (thr_eff-32)*13; } else { *throttle = 2908 + thr_eff*13; } if(grip_r < 0.85f){ *throttle = (short)(*throttle * (0.65f + grip_r * 0.35f)); } }
    }
    if(override_brake){ param_3[0] = param_3[0] | 4; int brk_eff = brake_override; if(grip_f < 0.85f){ brk_eff = (int)(brake_override * (0.50f + grip_f * 0.50f)); } if(brake_override >= 120 && grip_f < 0.80f){ brk_eff = (int)(brake_override * 0.55f); } *brake = brk_eff * 0x1000 / 127; }
    if(override_camera){ *camera_rotation = camera_override; }
    pspSdkSetK1(k1);
}
int _atoi(char *buf){ int i = 0; int val = 0; int negative = 0; if(buf[0] == '-'){ negative = 1; i = 1; } while(1){ if(buf[i] == '\0'){ break; } val = val * 10; val = val + (int)(buf[i] - '0'); i++; } if(negative){ val = val * (-1); } return val; }
void parse_config(){
    char *path = "ms0:/PSP/"MODULE_NAME"_settings.txt"; int fd = sceIoOpen(path, PSP_O_RDONLY, 0);
    if(fd < 0){ path = "ef0:/PSP/"MODULE_NAME"_settings.txt"; fd = sceIoOpen(path, PSP_O_RDONLY, 0); if(fd < 0){ return; } }
    char buf[128] = {0}; u32 len = sceIoRead(fd, buf, sizeof(buf)); if(len < 0){ return; }
    int arg_idx = 0; char arg_buf[128] = {0}; u32 arg_buf_write_head = 0;
    for(u32 i =0;i <= len; i++){ if(i < len && buf[i] != ' ' && buf[i] != '\n'){ arg_buf[arg_buf_write_head] = buf[i]; arg_buf_write_head++; } else{ if(arg_buf_write_head == 0){ continue; } arg_buf[arg_buf_write_head] = '\0';
        if(arg_idx == 0){ int num = _atoi(arg_buf); camera_controls = num && is_emulator; } else if(arg_idx == 1){ int num = _atoi(arg_buf); if(num > 127){ num = 127; } if(num < 0){ num = 0; } inner_deadzone = num; } else if(arg_idx == 2){ int num = _atoi(arg_buf); if(num > 127){ num = 127; } if(num < 0){ num = 0; } outer_deadzone = num; } arg_buf_write_head = 0; arg_idx++; } }
}
int init(){
    char disc_id[50]; char disc_version[50]; int disc_id_valid = get_disc_id_version(disc_id, disc_version) == 0; if(!disc_id_valid){ return -1; }
    if(set_offsets(disc_id, disc_version) != 0){ return -1; }
    if(is_emulator){ adjacent_axes = 1; outer_deadzone = 124; inner_deadzone = 3; }
    parse_config(); gt4_reset_all();
    SceUID hud_thid = sceKernelCreateThread("gt4_hud", gt4_hud_thread, 0x18, 0x10000, 0, NULL);
    if(hud_thid >= 0){ sceKernelStartThread(hud_thid, 0, NULL); }
    HIJACK_FUNCTION(offset_populate_car_analog_control, populate_car_analog_control_patched, populate_car_analog_control_orig);
    if(is_emulator){ sceKernelDelayThread(1000 * 1000 * 5); sceCtrlSetSamplingCycle(5555); }
    return 0;
}
#ifndef IRSHELL
int StartPSP(SceModule2 *mod) { char namebuf[sizeof(mod->modname) + 1] = {0}; memcpy(namebuf, mod->modname, sizeof(mod->modname)); if(strcmp(namebuf, GAME_MODULE_NAME) == 0){ game_base_addr = mod->text_addr; game_base_addr = game_base_addr + 0x28; init(); } if (!previous){ return 0; } return previous(mod); }
#else
int irshell_find_module(SceSize args, void *argp){ int cycles = 0; while(cycles < 60){ SceUID modules[256] = {0}; SceKernelModuleInfo info = {0}; int count = 0; if(sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0){ for(int i =0;i < count;i++){ info.size = sizeof(SceKernelModuleInfo); if(sceKernelQueryModuleInfo(modules[i], &info) < 0){ continue; } char namebuf[sizeof(info.name) + 1] = {0}; memcpy(namebuf, info.name, sizeof(info.name)); if(strcmp(namebuf, GAME_MODULE_NAME) == 0){ game_base_addr = info.text_addr + 0x28; } } if(game_base_addr != 0){ init(); sceKernelExitDeleteThread(0); return 0; } } sceKernelDelayThread(1000 * 1000); cycles++; } sceKernelExitDeleteThread(0); return 0; }
#endif
static void StartPPSSPP() { SceUID modules[256]; SceKernelModuleInfo info; int i, count = 0; if (sceKernelGetModuleIdList(modules, sizeof(modules), &count) >= 0){ for (i = 0; i < count; ++i){ info.size = sizeof(SceKernelModuleInfo); if (sceKernelQueryModuleInfo(modules[i], &info) < 0){ continue; } char namebuf[sizeof(info.name) + 1] = {0}; memcpy(namebuf, info.name, sizeof(info.name)); if(strcmp(namebuf, GAME_MODULE_NAME) == 0){ game_base_addr = info.text_addr; } } if(game_base_addr != 0){ init(); } } }
int module_start(SceSize args, void *argp){
    #if DEBUG_LOG
    sceIoRemove("ms0:/PSP/"MODULE_NAME".log"); sceIoRemove("ef0:/PSP/"MODULE_NAME".log");
    #endif
    is_emulator = sceIoDevctl("kemulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL, 0, NULL, 0) == 0;
    if (is_emulator){ StartPPSSPP(); }else{
        #ifndef IRSHELL
        previous = sctrlHENSetStartModuleHandler(StartPSP);
        #else
        SceUID thid = sceKernelCreateThread("irshell_find_module", irshell_find_module, 0x18, 0x10000, 0, NULL); if(thid < 0){ return 0; } sceKernelStartThread(thid, 0, NULL);
        #endif
    }
    return 0;
}
int module_stop(SceSize args, void *argp){ return 0; }
