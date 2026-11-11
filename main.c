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

    // === TILT BAWAAN PRX ASLI - JANGAN DIUBAH, TANPA FILTER ===
    if(lxp > 0){ override_steering = 1; steering_override = -lxp; }
    if(lxn > 0){ override_steering = 1; steering_override = lxn; }
    // === SAMPAI SINI ASLI ===

    // Cuma gas/rem digital yang ditambah, setir gak disentuh
    u32 btn = pad_data[count - 1].Buttons;
    if(btn & PSP_CTRL_CROSS){ override_accel = 1; accel_override = 127; }
    if(btn & PSP_CTRL_SQUARE){ override_brake = 1; brake_override = 127; }

    // Stick kanan cuma kalau tombol gak dipencet
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
