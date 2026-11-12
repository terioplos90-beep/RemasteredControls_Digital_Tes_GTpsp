#include <pspctrl.h>
#include <string.h>
#include "hooking.h"
#include "disc_ident.h"
#include "logging.h"
#include <systemctrl.h>
#include "common.h"
#define GAME_MODULE_NAME "PDIAPP"
PSP_MODULE_INFO(MODULE_NAME, PSP_MODULE_KERNEL, 1, 1);
#define EMULATOR_DEVCTL__IS_EMULATOR 0x00000000
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
static float gt4_total_km = 0;
static float gt4_wear[4] = {0,0,0,0};
static float gt4_speed = 0;
static void gt4_update(float thr, float brk, float steer){
 gt4_speed = gt4_speed + thr*0.8f - brk*0.5f;
 if(gt4_speed<0) gt4_speed=0;
 if(gt4_speed>250) gt4_speed=250;
 float dist = gt4_speed*0.016f/3.6f/1000.0f*60.0f;
 gt4_total_km += dist;
 float s = steer<0?-steer:steer;
 float load = 1.0f + thr*1.5f + brk*1.0f + s*1.0f;
 for(int i=0;i<4;i++){
  gt4_wear[i] += dist*load*1500.0f;
  if(gt4_wear[i]>100) gt4_wear[i]=100;
 }
 static int c=0;
 if((c++%60)==0){
  LOG("GT4 TIRE | FL:%d FR:%d RL:%d RR:%d | KM:%d THR:%d",
  (int)gt4_wear[0],(int)gt4_wear[1],(int)gt4_wear[2],(int)gt4_wear[3],
  (int)gt4_total_km,(int)(thr*127));
 }
}
static int apply_deadzone(int val){
 if(val<inner_deadzone) return 0;
 int range = outer_deadzone-inner_deadzone;
 val = val-inner_deadzone;
 if(val>range) val=range;
 return val*127/range;
}
static void sample_input(SceCtrlData *pad_data, int count, int negative){
 if(count < 1) return;
 int rx = pad_data[count-1].Rsrv[0];
 int ry = pad_data[count-1].Rsrv[1];
 int lx = pad_data[count-1].Lx;
 int ly = pad_data[count-1].Ly;
 int lxp=0,lxn=0,lyp=0,lyn=0,rxn=0,ryp=0,ryn=0;
 static int right_stick_looks_dead = 1;
 if(right_stick_looks_dead && (rx!=0||ry!=0)) right_stick_looks_dead=0;
 if(lx<128) lxn=apply_deadzone(128-lx);
 if(lx>128) lxp=apply_deadzone(lx-128);
 if(ly<128) lyn=apply_deadzone(128-ly);
 if(ly>128) lyp=apply_deadzone(ly-128);
 if(rx<128) rxn=apply_deadzone(128-rx);
 if(ry<128) ryn=apply_deadzone(128-ry);
 if(ry>128) ryp=apply_deadzone(ry-128);
 if(adjacent_axes){ int tmp=ryn; ryn=rxn; rxn=tmp; }
 override_brake=0; override_accel=0;
 override_steering=0; override_camera=0;
 if(lxp>0){ override_steering=1; steering_override=lxp*(-1); }
 if(lxn>0){ override_steering=1; steering_override=lxn; }
 if(!right_stick_looks_dead){
  if(ryp>0){ override_brake=1; brake_override=ryp; }
  if(ryn>0){ override_accel=1; accel_override=ryn; }
 }
 if(camera_controls){
  if(lyn>0){ override_camera=1; camera_override=(float)(lyn*-1.5f)/127.0f; }
  if(lyp>0){ override_camera=1; camera_override=(float)(lyp*1.5f)/127.0f; }
 }
}int set_offsets(char *disc_id, char *disc_version){
 if(strcmp("2.00",disc_version)==0 && (strcmp("UCES01245",disc_id)==0 || strcmp("UCUS98632",disc_id)==0)){
  offset_digital_to_analog = game_base_addr + 0x14eb40;
  offset_populate_car_digital_control = game_base_addr + 0x126b50;
  offset_populate_car_analog_control = game_base_addr + 0x126dec;
  return 0;
 }
 if(strcmp("1.00",disc_version)==0 && strcmp("UCAS40265",disc_id)==0){
  offset_populate_car_analog_control = game_base_addr + 0x126dec; return 0;
 }
 if(strcmp("1.01",disc_version)==0 && strcmp("UCJS10100",disc_id)==0){
  offset_populate_car_analog_control = game_base_addr + 0x126dd0; return 0;
 }
 return -1;
}
static void (*populate_car_analog_control_orig)(u32 p1, int *p2, unsigned char *p3, u32 p4, u32 p5, unsigned char p6);
void populate_car_analog_control_patched(u32 p1, int *p2, unsigned char *p3, u32 p4, u32 p5, unsigned char p6){
 short *steering=(short*)(&p3[4]);
 float *camera_rotation=(float*)(&p3[0x2c]);
 short *throttle=(short*)(&p3[0x8]);
 short *brake=(short*)(&p3[0xA]);
 p3[0]=0; p3[1]=0;
 int k1=pspSdkSetK1(0);
 int mode; sceCtrlGetSamplingMode(&mode);
 if(mode!=PSP_CTRL_MODE_ANALOG){ sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG); return; }
 { SceCtrlData pad_data; int res=sceCtrlPeekBufferPositive(&pad_data,1); sample_input(&pad_data,res,0); }
 { float thr=override_accel?(float)accel_override/127.0f:0;
   float brk=override_brake?(float)brake_override/127.0f:0;
   float str=override_steering?(float)steering_override/127.0f:0;
   gt4_update(thr,brk,str); }
 if(override_steering){ p3[0]|=1; p3[1]|=2; *steering=steering_override*0x2000/127; }
 else { *steering=0; }
 if(override_accel){
  p3[0]|=2;
  if(accel_override>=95){ *throttle=3932 + (accel_override-95)*164/32; if(*throttle>4096) *throttle=4096; }
  else if(accel_override>=64){ *throttle=3727 + (accel_override-64)*204/30; }
  else if(accel_override>=32){ *throttle=3317 + (accel_override-32)*409/31; }
  else { *throttle=2908 + accel_override*409/31; }
 }
 if(override_brake){ p3[0]|=4; *brake=brake_override*0x1000/127; }
 if(override_camera){ *camera_rotation=camera_override; }
 pspSdkSetK1(k1);
}
int _atoi(char *buf){
 int i=0,val=0,neg=0;
 if(buf[0]=='-'){neg=1;i=1;}
 while(buf[i]!='\0'){ val=val*10; val=val+(buf[i]-'0'); i++; }
 if(neg) val=val*(-1); return val;
}
void parse_config(){
 char *path="ms0:/PSP/"MODULE_NAME"_settings.txt";
 int fd=sceIoOpen(path,PSP_O_RDONLY,0);
 if(fd<0){ path="ef0:/PSP/"MODULE_NAME"_settings.txt"; fd=sceIoOpen(path,PSP_O_RDONLY,0); if(fd<0) return; }
 char buf[128]={0}; u32 len=sceIoRead(fd,buf,sizeof(buf)); if(len<0) return;
 int arg_idx=0; char arg_buf[128]={0}; u32 h=0;
 for(u32 i=0;i<=len;i++){
  if(i<len && buf[i]!=' ' && buf[i]!='\n'){ arg_buf[h]=buf[i]; h++; }
  else { if(h==0) continue; arg_buf[h]='\0';
   if(arg_idx==0){ camera_controls=_atoi(arg_buf) && is_emulator; }
   else if(arg_idx==1){ int n=_atoi(arg_buf); if(n>127)n=127; if(n<0)n=0; inner_deadzone=n; }
   else if(arg_idx==2){ int n=_atoi(arg_buf); if(n>127)n=127; if(n<0)n=0; outer_deadzone=n; }
   h=0; arg_idx++; }
 }
}
int init(){
 char disc_id[50]; char disc_version[50];
 if(get_disc_id_version(disc_id,disc_version)!=0) return -1;
 if(set_offsets(disc_id,disc_version)!=0) return -1;
 if(is_emulator){ adjacent_axes=1; outer_deadzone=124; inner_deadzone=3; }
 parse_config();
 HIJACK_FUNCTION(offset_populate_car_analog_control, populate_car_analog_control_patched, populate_car_analog_control_orig);
 if(is_emulator){ sceKernelDelayThread(1000*1000*5); sceCtrlSetSamplingCycle(5555); }
 return 0;
}
#ifndef IRSHELL
int StartPSP(SceModule2 *mod){
 char nb[sizeof(mod->modname)+1]={0}; memcpy(nb,mod->modname,sizeof(mod->modname));
 if(strcmp(nb,GAME_MODULE_NAME)==0){ game_base_addr=mod->text_addr+0x28; init(); }
 if(!previous) return 0; return previous(mod);
}
#else
int irshell_find_module(SceSize args, void *argp){
 int c=0; while(c<60){
  SceUID m[256]={0}; SceKernelModuleInfo info={0}; int cnt=0;
  if(sceKernelGetModuleIdList(m,sizeof(m),&cnt)>=0){
   for(int i=0;i<cnt;i++){ info.size=sizeof(SceKernelModuleInfo); if(sceKernelQueryModuleInfo(m[i],&info)<0) continue; char nb[sizeof(info.name)+1]={0}; memcpy(nb,info.name,sizeof(info.name)); if(strcmp(nb,GAME_MODULE_NAME)==0) game_base_addr=info.text_addr+0x28; }
   if(game_base_addr!=0){ init(); sceKernelExitDeleteThread(0); return 0; }
  }
  sceKernelDelayThread(1000*1000); c++;
 }
 sceKernelExitDeleteThread(0); return 0;
}
#endif
static void StartPPSSPP(){
 SceUID m[256]; SceKernelModuleInfo info; int cnt=0;
 if(sceKernelGetModuleIdList(m,sizeof(m),&cnt)>=0){
  for(int i=0;i<cnt;i++){ info.size=sizeof(SceKernelModuleInfo); if(sceKernelQueryModuleInfo(m[i],&info)<0) continue; char nb[sizeof(info.name)+1]={0}; memcpy(nb,info.name,sizeof(info.name)); if(strcmp(nb,GAME_MODULE_NAME)==0) game_base_addr=info.text_addr; }
  if(game_base_addr!=0) init();
 }
}
int module_start(SceSize args, void *argp){
 is_emulator = sceIoDevctl("kemulator:", EMULATOR_DEVCTL__IS_EMULATOR, NULL,0,NULL,0)==0;
 if(is_emulator){ StartPPSSPP(); }
 else{
  #ifndef IRSHELL
  previous = sctrlHENSetStartModuleHandler(StartPSP);
  #else
  SceUID thid=sceKernelCreateThread("irshell_find_module", irshell_find_module, 0x18, 0x10000, 0, NULL);
  if(thid>=0) sceKernelStartThread(thid,0,NULL);
  #endif
 }
 return 0;
}
int module_stop(SceSize args, void *argp){ return 0; }
