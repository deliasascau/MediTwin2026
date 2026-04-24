#pragma once
#include <stdint.h>

bool initMPU6050();
bool readMpu6050(float &accelG, float &gx, float &gy, float &gz);

// Calibrare gyro baseline pe durata `durationMs`.
// Daca detecteaza miscare, reincearca automat de mai multe ori.
// Returneaza false doar daca nu exista citiri valide (MPU absent/nefunctional).
bool calibrateMpuGyroBaseline(float &gxBase, float &gyBase, float &gzBase,
							  uint32_t durationMs = 500);
