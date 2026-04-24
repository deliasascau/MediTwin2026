#pragma once

void  initHCSR04();
float readDistance();  // cm; returns -1.0 on timeout / out of range

// Calibrare la boot: media citirilor valide devine baseline.
bool  hcsr04Calibrate(int samples = 20);
float hcsr04Baseline();
