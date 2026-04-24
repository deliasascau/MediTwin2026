#pragma once

// Returns current in Amperes. Near zero at no load.
float readAcs712();

// Calibrare zero-current (fan/load OFF): media `samples` devine noul midpoint.
void  acs712Calibrate(int samples = 200);

// Midpoint curent (ADC raw) folosit intern la conversie.
int   acs712MidpointRaw();
