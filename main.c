  // --- TES BAN, 5 BARIS DOANG ---
  float thr_test = 0;
  if(pad->Ly < 120) thr_test = 1;
  if(pad->Buttons & PSP_CTRL_CROSS) thr_test = 1;

  float steer_test = 0;
  if(pad->Buttons & PSP_CTRL_LEFT) steer_test = 1;
  if(pad->Buttons & PSP_CTRL_RIGHT) steer_test = 1;

  wear_test = wear_test + thr_test + steer_test;
  if(wear_test > 200) pad->Ly = 128; // kalau udah 200, gas dimatiin total
  // --- SELESAI TES ---
