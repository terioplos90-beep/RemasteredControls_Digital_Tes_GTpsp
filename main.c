// ===== FUNGSI BAN GT4 - MODE TES 1 LAP HABIS =====
#define GT4_TEST_MODE 1
#if GT4_TEST_MODE
  #define GT4_BAN_HABIS_KM 1.0f // TES: 1 KM = 100%
#else
  #define GT4_BAN_HABIS_KM 7.0f // ASLI GT4: 7 KM = 100%
#endif

typedef struct { float wear; float temp; float grip; int stage; int is_flat; } TireGT4;
static TireGT4 tireFL, tireFR, tireRL, tireRR;
static float total_km_gt4 = 0.0f;
static int gt4_initialized = 0;
static float last_speed_gt4 = 0.0f;
static float last_steer_gt4 = 0.0f;

static float gt4_absf(float v){ return v < 0 ? -v : v; }
static float gt4_clampf(float v, float mn, float mx){ if(v < mn) return mn; if(v > mx) return mx; return v; }

static void gt4_reset_all(){
  tireFL.wear=0; tireFL.temp=50; tireFL.grip=1.05f; tireFL.stage=0; tireFL.is_flat=0;
  tireFR.wear=0; tireFR.temp=50; tireFR.grip=1.05f; tireFR.stage=0; tireFR.is_flat=0;
  tireRL.wear=0; tireRL.temp=50; tireRL.grip=1.05f; tireRL.stage=0; tireRL.is_flat=0;
  tireRR.wear=0; tireRR.temp=50; tireRR.grip=1.05f; tireRR.stage=0; tireRR.is_flat=0;
  total_km_gt4=0; gt4_initialized=1;
}

static float gt4_calc_grip(TireGT4 *t){
  if(t->is_flat) return 0.30f;
  // Logika GT4: 0-30% grip naik dikit, 30-65 turun pelan, 65-85 kerasa, 85+ drop
  float g;
  if(t->wear < 30) g = 1.05f + (t->wear / 30.0f) * 0.02f;
  else if(t->wear < 65) g = 1.07f - ((t->wear - 30) / 35.0f) * 0.12f;
  else if(t->wear < 85) g = 0.95f - ((t->wear - 65) / 20.0f) * 0.25f;
  else g = 0.70f - ((t->wear - 85) / 15.0f) * 0.40f;
  // Suhu: dingin kurang grip, kepanasan juga kurang grip
  if(t->temp < 50) g -= 0.05f;
  if(t->temp > 125) g -= 0.15f;
  // Stage buat HUD: 0=biru/hijau, 1=kuning, 2=orange, 3=merah
  if(t->wear < 30) t->stage=0;
  else if(t->wear < 65) t->stage=1;
  else if(t->wear < 85) t->stage=2;
  else t->stage=3;
  return gt4_clampf(g, 0.30f, 1.07f);
}

static void gt4_update(float steering_norm, float throttle_norm, float brake_norm, float dt){
  if(!gt4_initialized) gt4_reset_all();
  float speed = throttle_norm * 280.0f;
  if(brake_norm > 0.1f) speed = last_speed_gt4 - brake_norm * 100.0f * dt;
  else speed = last_speed_gt4 + (speed - last_speed_gt4) * dt * 0.5f;
  speed = gt4_clampf(speed, 0, 320);
  last_speed_gt4 = speed;

  float jarak_meter = (speed / 3.6f) * dt;
  total_km_gt4 += jarak_meter / 1000.0f;

  float slip = gt4_absf(steering_norm) * (speed / 180.0f);
  float steer_delta = gt4_absf(steering_norm - last_steer_gt4) / dt;
  int is_drifting = (slip > 0.7f && speed > 80);
  int is_wheelspin = (throttle_norm > 0.9f && speed < 120);
  int is_braking_hard = (brake_norm > 0.7f && speed > 80);

  TireGT4* list[4] = {&tireFL, &tireFR, &tireRL, &tireRR};
  float beban[4] = {1.0f, 1.0f, 0.85f, 0.85f};
  if(steering_norm > 0.2f) { beban[0] = 1.0f + steering_norm * 1.4f; beban[2] = beban[0] * 0.85f; }
  if(steering_norm < -0.2f) { beban[1] = 1.0f + gt4_absf(steering_norm) * 1.4f; beban[3] = beban[1] * 0.85f; }
  int is_front[4] = {1,1,0,0};

  for(int i=0;i<4;i++){
    TireGT4 *t = list[i];
    if(t->is_flat) continue;
    // Suhu naik dari slip + kecepatan, turun karena pendinginan
    float heat = (speed/300.0f)*7.0f*dt + slip*22.0f*dt;
    if(is_front[i]) heat *= 1.35f;
    t->temp += heat - (2.8f*dt);
    t->temp = gt4_clampf(t->temp, 20, 160);

    // Wear: base dari jarak + slip + drift + rem
    float w = (jarak_meter / (GT4_BAN_HABIS_KM * 1000.0f)) * 100.0f;
    w *= beban[i];
    if(slip > 0.2f) w *= (1.0f + slip * 0.5f);
    if(t->
    if(t->is_flat) continue;
    float heat = (speed/300.0f)*7.0f*dt + slip*22.0f*dt;
    if(is_front[i]) heat *= 1.35f;
    t->temp += heat - (2.8f*dt);
    t->temp = gt4_clampf(t->temp, 20, 160);
    float w = (jarak_meter / (GT4_BAN_HABIS_KM * 1000.0f)) * 100.0f;
    w *= beban[i];
    if(slip > 0.2f) w *= (1.0f + slip * 0.5f);
    if(t->temp > 130) w *= 1.5f;
    if(is_front[i]) w *= 1.05f; else w *= 1.15f;
    t->wear += w;
    t->wear = gt4_clampf(t->wear, 0, 100);
    t->grip = gt4_calc_grip(t);
  }
  if(is_braking_hard){ tireFL.wear += 0.02f; tireFR.wear += 0.02f; }
  if(is_drifting){ tireRL.wear += 0.03f; tireRR.wear += 0.03f; }
  if(is_wheelspin){ tireRL.wear += 0.02f; tireRR.wear += 0.02f; }
  tireFL.wear = gt4_clampf(tireFL.wear,0,100);
  tireFR.wear = gt4_clampf(tireFR.wear,0,100);
  tireRL.wear = gt4_clampf(tireRL.wear,0,100);
  tireRR.wear = gt4_clampf(tireRR.wear,0,100);
  last_steer_gt4 = steering_norm;
}
static float gt4_front_grip(){ return (tireFL.grip + tireFR.grip) / 2.0f; }
static float gt4_rear_grip(){ return (tireRL.grip + tireRR.grip) / 2.0f; }
