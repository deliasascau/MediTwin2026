#pragma once

// Circuit: 3V3 → LDR → GPIO0 → 10kΩ → GND
//   Dark   → rezistenta LDR mare  → ADC mic  (~0)
//   Bright → rezistenta LDR mica  → ADC mare (~4095)
//
// La pornire se presupune lumina NORMALA.
// ldrCalibrate() stocheaza baseline-ul; pragurile DARK/DIM/BRIGHT
// devin procente relative din acest baseline.

// Citeste ADC brut (0–4095)
int     readLdr();

// Calibrare la pornire: face `samples` citiri, stocheaza media ca baseline
void    ldrCalibrate(int samples = 20);

// Returneaza baseline-ul stocat (0 daca nu s-a calibrat inca)
int     ldrBaseline();

// Eticheta textuala a unui raw ADC pe baza baseline-ului calibrat
const char* ldrLabel(int raw);
