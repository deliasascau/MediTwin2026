# HC-SR04 — Ultrasonic Distance / Presence Sensor

## Ce este

Senzor ultrasonic care măsoară distanța față de un obiect prin ecou.  
Emite un puls de 40 kHz și măsoară timpul până la întoarcerea ecoului.

Folosit în MediTwin pentru **detecție prezență** (persoană în cameră).

---

## Pini

| Pin | Funcție |
|---|---|
| VCC | 5V |
| GND | GND |
| TRIG | Trigger — puls de 10µs de la ESP32 (output) |
| ECHO | Echo — puls HIGH proporțional cu distanța (input) |

> ⚠️ **ECHO iese la 5V** — GPIO-urile ESP32-C6 sunt tolerate doar la 3.3V.  
> **Obligatoriu divizor de tensiune pe ECHO!** Altfel riști să arzi GPIO-ul.

---

## Schema circuitului

```
ESP32-C6         HC-SR04
  5V   ─────────  VCC
  GND  ─────────  GND
  GPIO11 ────────  TRIG      (OUTPUT — 3.3V e suficient pentru trigger)

  ECHO ──── [1kΩ] ──── GPIO12 (INPUT)
                │
             [2kΩ]
                │
               GND
```

Divizorul 1kΩ / 2kΩ reduce 5V → 3.33V:  
`V_out = 5V × 2/(1+2) = 3.33V` ✓

---

## Montare pe breadboard — pas cu pas

```
HC-SR04 modul  →  Breadboard  →  Destinație
──────────────────────────────────────────────────
VCC            →  rând 5 A    →  fir 5V (ESP32)
GND            →  rând 5 B    →  fir GND
TRIG           →  rând 5 C    →  fir GPIO11 (direct)
ECHO           →  rând 5 D    →  [1kΩ] rând 8
                               →  rând 8 ─── fir GPIO12
                               →  [2kΩ] rând 8 la rând 11
                               →  rând 11 ─── GND
```

**Detaliu divizor de tensiune:**
```
HC-SR04 ECHO
    │
  [1kΩ]   ← rândul 8 breadboard
    │
  ──┬────── fir la GPIO12 (ESP32)
  [2kΩ]   ← rândul 11 breadboard
    │
   GND
```

---

## Conexiuni ESP32-C6

| Pin HC-SR04 | GPIO ESP32-C6 | Note |
|---|---|---|
| VCC | **5V** | |
| GND | **GND** | |
| TRIG | **GPIO 11** | Output, direct |
| ECHO | **GPIO 12** prin divizor 1kΩ/2kΩ | Input — **nu direct!** |

---

## Cum funcționează codul

```
1. TRIG LOW  2µs        → resetare
2. TRIG HIGH 10µs       → trimite burst ultrasonic
3. TRIG LOW             → stop trigger
4. pulseIn(ECHO, HIGH)  → măsoară durata pulsului de echo (µs)
5. distanță = (durata × 0.0343) / 2  (cm)
   → 0.0343 cm/µs = viteza sunetului
   → împărțit la 2 pentru că pulsul face dus-întors
```

Timeout: 30,000 µs → ~5 m maxim.  
Funcția returnează `-1.0` dacă nu vine ecou (obiect prea departe sau lipsă).

---

## Specificații tehnice

| Parametru | Valoare |
|---|---|
| Tensiune alimentare | 5V DC |
| Unghi de detectare | 15° (con) |
| Distanță minimă | ~2 cm |
| Distanță maximă | ~400 cm |
| Precizie | ±3 mm |
| Frecvență impuls | 40 kHz |

---

## Verificare funcționare

Uploadează `meditwin_test`, trimite `3`.

| Situație | Valoare așteptată |
|---|---|
| Mâna la 10 cm | `9–12 cm` |
| Mâna la 50 cm | `48–52 cm` |
| Fără obiect în față | `no echo / -1` |
| Obiect la < 80 cm | `[PRESENCE]` |

---

## Calibrare

HC-SR04 nu necesită calibrare electronică.  
Calibrezi **pragul de prezență** în `config.h`:

```c
// config.h
#define THRESH_PRESENCE_CM  80.0f  // prezență dacă distanță < 80 cm
```

**Cum alegi valoarea:**
- Măsoară distanța de la senzor la ușa camerei sau la pat
- Setează pragul la ~70–80% din acea distanță
- Ex: ușa la 150 cm → prag la 120 cm

---

## Probleme frecvente

| Simptom | Cauză probabilă | Fix |
|---|---|---|
| Distanță mereu `-1` | GND lipsă sau TRIG neconectat | Verifică cablajul, mai ales GND |
| Distanță mereu 0 | ECHO la 5V pe GPIO fără divizor | Adaugă divizor 1kΩ/2kΩ |
| Valori care sar mult | Suprafață absorbantă (textil) sau unghi | Îndreptă senzorul direct spre obiect |
| Citiri < 2 cm | Obiect prea aproape (blind spot) | Senzorul nu poate măsura sub 2 cm |
| Valori inconsistente | Reflexii multiple în spațiu mic | Normal — filtrare în cod |
| ESP32 se resetează | ECHO la 5V direct | Adaugă **imediat** divizorul de tensiune |

---

## Note pentru proiect MediTwin

HC-SR04 detectează **prezența persoanei în cameră**:
- Distanță < prag → cameră ocupată → risc crescut
- Input în modelul AI (contribuție +15 la scorul de risc)
- Diferențiază scenariile: cameră goală vs. cameră cu pacient

**Pentru demo**: stai/intră în câmpul de vizibilitate al senzorului → sistemul sesizează prezența și ajustează scorul de risc.

Pin definit în `config.h`: `PIN_TRIG = GPIO11`, `PIN_ECHO = GPIO12`
