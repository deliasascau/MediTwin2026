# MediTwin2026 - Project Status (24 Apr 2026)

## 1. Scopul proiectului
MediTwin ruleaza pe ESP32-C6 si face:
- achizitie date senzori medicali/ambientali
- evaluare stari de risc local (fallback de siguranta)
- comanda actuatoare (LED, buzzer, fan)
- telemetrie JSON spre Raspberry Pi prin UART

Pi poate lua decizii de nivel aplicatie si trimite comenzi inapoi.

## 2. Hardware actual
### Senzori
- LDR pe GPIO0 (lumina)
- MQ-3 pe GPIO1 (calitate aer)
- ACS712 pe GPIO2 (curent fan)
- DHT22 pe GPIO10 (temperatura, umiditate)
- HC-SR04 pe GPIO11/12 (distanta/fluctuatii)
- MPU6050 pe I2C GPIO8/9 (miscare)

Nota: in cod, denumirea istorica pentru MQ-3 este pastrata in identificatori ca mq135/PIN_MQ135 pentru compatibilitate.

### Actuatoare
- LED (3 canale) pe GPIO6/4/5
- Buzzer pe GPIO18
- Fan pe GPIO19
- Buton unic pe GPIO15 (INPUT_PULLUP, activ LOW)

### Comunicatie
- UART1 spre Raspberry Pi: TX GPIO22, RX GPIO21, 115200 baud

## 3. Firmware-uri active
### Production
- entrypoint: src/main.cpp
- environment: meditwin_esp32c6

### Test/Monitor extins
- entrypoint: src/sensor_test.cpp
- environment: meditwin_test
- include monitor live extins si scenarii de verificare rapida

## 4. Calibrare automata la fiecare boot
La pornire se ruleaza automat:
1. LDR baseline (lumina normala la boot)
2. MQ-3 warmup + baseline
3. DHT22 baseline (medie din citiri valide)
4. HC-SR04 baseline (medie distanta valida)
5. ACS712 midpoint (zero-current)
6. MPU6050 gyro baseline

Acest comportament este activ atat in production, cat si in test monitor.

## 5. FSM generic (production + modul de baza)
FSM generic are stari:
- OK
- WARNING
- CRITICAL
- MOTION

Tranzitii principale:
- sensor fault DHT/MPU dupa ce au fost valide -> CRITICAL
- fan ON si currentA sub prag minim -> CRITICAL
- MQ-3 peste prag critic dinamic -> CRITICAL
- temperatura peste prag critic -> CRITICAL
- gyro magnitude peste prag -> MOTION
- MQ-3 peste prag warning dinamic -> WARNING
- temperatura peste prag warning dinamic -> WARNING
- altfel -> OK

Actuatoare FSM generic:
- OK: LED verde, buzzer off
- WARNING: LED portocaliu, beep periodic
- CRITICAL: LED rosu, beep rapid
- MOTION: LED rosu, pattern alerta

## 6. Monitor extins (sensor_test)
Pe langa FSM generic, monitorul extins gestioneaza scenarii clinice custom:
- MQ-3 critical: CRITICAL, fan ON, buzzer ON, LED rosu
- LDR prea mare: CRITICAL
- HC flatline (fara fluctuatii): CRITICAL
- DHT temperatura ridicata: WARNING
- MPU miscare peste prag: CRITICAL
- ACS712 fan no current: CRITICAL

## 7. Auto-recovery vs manual ACK
### Auto-recovery activ
- HC flatline: revine automat daca fluctuatia reapare in fereastra valida
- LDR: revine automat daca lumina revine sub prag cu histerezis
- MPU: revine automat daca gyro revine sub prag de recover
- DHT: revine automat daca temperatura revine sub prag cu histerezis

### Latch manual (necesita buton)
- MQ-3 critical
- Fan no current (ACS712)

## 8. Buton GPIO15
- Apasare scurta: ACK alarma (reset monitor extins)
- Apasare medie: ACK/rearm
- Apasare lunga: standby toggle

## 9. Telemetrie spre Raspberry Pi
Payload JSON include campuri:
- ts, temp, hum, air, light, dist
- accel, gx, gy, gz
- cur
- dht, mpu
- state, risk

Comenzi primite de la Pi:
- CMD:FAN_ON
- CMD:FAN_OFF
- CMD:ALARM_ON
- CMD:ALARM_OFF
- CMD:FSM_RESET

## 10. Cazuri de test recomandate (pas cu pas)
1. SAFE steady
- conditii normale
- asteptat: LED verde, buzzer OFF, stare SAFE/OK

2. DHT warning
- crestere controlata temperatura
- asteptat: WARNING, LED galben/portocaliu, buzzer ON
- revenire: auto dupa normalizare sau buton

3. MQ-3 critical
- crestere rapida aer (vapori/alcool controlat)
- asteptat: CRITICAL, LED rosu, buzzer ON, fan ON
- revenire: doar buton (si recalibrare MQ-3)

4. LDR critical
- lumina puternica directa
- asteptat: CRITICAL
- revenire: auto dupa lumina normala sau buton

5. MPU critical
- miscare peste prag
- asteptat: CRITICAL
- revenire: auto dupa stabilizare sau buton

6. HC no-respiration (flatline)
- dupa grace period, tine distanta aproape fixa
- asteptat: CRITICAL (HC flatline)
- revenire: auto cand reapar fluctuatii sau buton

7. Fan current fault
- in scenariu cu fan fortat ON, elimina curentul masurat
- asteptat: CRITICAL fan no current
- revenire: manual (buton)

## 11. Parametri importanti (config)
- MQ135_OFFSET_WARNING / MQ135_OFFSET_CRITICAL
- THRESH_TEMP_WARNING / THRESH_TEMP_CRITICAL
- DHT_TEMP_RISE_WARNING_C
- HC_STARTUP_GRACE_MS / HC_FLATLINE_WINDOW_MS / HC_MIN_SPREAD_CM
- MPU_GYRO_CRITICAL_THRESH
- FAN_CURRENT_MIN_A
- AUTO_CLEAR_HOLD_MS + histerezis LDR/MPU/DHT

## 12. Observatii curente
- Sistemul compileaza pe ambele environment-uri.
- Upload pe COM8 functioneaza, dar portul poate disparea temporar in Windows; se recomanda reconnect USB si relansare monitor.
- Monitorul extins afiseaza live valorile tuturor senzorilor si trigger-ele pentru cauza starii.
