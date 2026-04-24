#pragma once

// MQ-3 module (legacy file name kept for compatibility).
// Returns raw ADC value (0–4095). Higher = worse air quality.
int  readMq135();

// Calibrare la pornire (dupa warmup): face `samples` citiri, stocheaza media
// ca baseline = "aer normal la pornire".
void mq135Calibrate(int samples = 20);

// Returneaza baseline-ul stocat (0 daca nu s-a calibrat).
int  mq135Baseline();

// Praguri dinamice (baseline + offset):
//   Warning  = baseline + MQ135_OFFSET_WARNING
//   Critical = baseline + MQ135_OFFSET_CRITICAL
int  mq135WarnThresh();
int  mq135CritThresh();
