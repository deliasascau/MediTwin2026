# DHT22 — Temperature & Humidity Sensor

## Ce este

Senzor digital combinat de temperatură și umiditate.  
Comunică printr-un protocol single-wire proprietar (nu I2C, nu SPI — un singur fir de date).

Folosit în MediTwin ca **senzor primar de temperatură și umiditate ambientală**.

---

## Pini (DHT22 / AM2302)

| Pin | Funcție | Note |
|---|---|---|
| 1 — VCC | Alimentare | 3.3V sau 5V |
| 2 — DATA | Date single-wire | Necesită pull-up 10kΩ la VCC |
| 3 — NC | Neconectat | Lasă liber |
| 4 — GND | Masă | |

> Modulele cu PCB au deja rezistorul pull-up montat. Dacă ai **doar senzorul** (fără modul), adaugă tu 10kΩ între DATA și VCC.

---

## Schema circuitului

### Fără modul PCB (senzor gol):
```
3V3 ──────┬───── VCC (pin 1)
          │
         [10kΩ]  ← pull-up
          │
GPIO10 ───┴───── DATA (pin 2)

                  NC   (pin 3) — neconectat
GND ──────────── GND  (pin 4)
```

### Cu modul PCB (are deja pull-up):
```
3V3 ─────── VCC
GND ─────── GND
GPIO10 ──── DATA (sau S)
```

---

## Montare pe breadboard — pas cu pas

**Cu senzor gol (fără modul PCB):**

DHT22 are 4 picioare. Privind din față (grila):
```
[ 1 ][ 2 ][ 3 ][ 4 ]
 VCC  DATA  NC  GND
```

```
Componentă          Breadboard     Destinație
──────────────────────────────────────────────────
DHT22 pin 1 (VCC)  → rând 5 A  →  fir 3V3 (ESP32)
DHT22 pin 2 (DATA) → rând 5 B  →  fir GPIO10 (ESP32)
DHT22 pin 3 (NC)   → liber
DHT22 pin 4 (GND)  → rând 5 D  →  fir GND (ESP32)
Rezistor 10kΩ      → rând 5 B (DATA) la rând 5 A (VCC)
```

**Detaliu pull-up:**
```
3V3
 │
[10kΩ]
 │
 ├──── fir la GPIO10 (ESP32)
 │
[DHT22 DATA]
```

---

## Conexiuni ESP32-C6

| Pin DHT22 | GPIO ESP32-C6 | Note |
|---|---|---|
| VCC | **3V3** | 3.3V e suficient |
| DATA | **GPIO 10** | Cu 10kΩ pull-up la 3V3 |
| NC | — | Neconectat |
| GND | **GND** | |

---

## Specificații tehnice

| Parametru | Valoare |
|---|---|
| Temperatură | -40 – +80 °C |
| Precizie temperatură | ±0.5 °C |
| Umiditate | 0 – 100 %RH |
| Precizie umiditate | ±2–5 %RH |
| Interval minim citire | **2 secunde** |
| Timp răspuns | 2 s |
| Alimentare | 3.3V – 5.5V |

> ⚠️ **Intervalul minim de citire este 2 secunde.** Dacă citești mai des, senzorul returnează eroare (NaN). Codul setează `delay(2200)` între citiri.

---

## Verificare funcționare

Uploadează `meditwin_test`, trimite `4`.

| Test | Rezultat așteptat |
|---|---|
| Temperatura ambientală | 20–26°C (depinde de cameră) |
| Umiditate normală | 40–60%RH |
| Sufli pe senzor | Umiditatea crește vizibil în 2–4 s |
| Ții senzorul în mână | Temperatura crește ușor |

---

## Calibrare

DHT22 are o precizie bună din fabrică (±0.5°C). Nu necesită calibrare hardware.

Poți adăuga un **offset software** dacă observi o abatere sistematică față de un termometru de referință:

```c
// config.h  — offset dacă senzorul tău are abatere sistematică
#define DHT22_TEMP_OFFSET   0.0f   // ex: -0.5 dacă citește prea mare
#define DHT22_HUM_OFFSET    0.0f
```

Și în `dht22.cpp`:
```cpp
bool readDht(float &temp, float &hum) {
    hum  = dht.readHumidity()    + DHT22_HUM_OFFSET;
    temp = dht.readTemperature() + DHT22_TEMP_OFFSET;
    ...
}
```

---

## Praguri pentru proiect MediTwin

```c
// config.h — standard camere medicale / izolare
#define THRESH_TEMP_WARNING   28.0f   // °C — temperatură ridicată
#define THRESH_TEMP_CRITICAL  32.0f   // °C — pericol termic
```

Standardele pentru camere de spital recomandă 20–24°C. Pragul de 28°C este conservator pentru demo.

---

## Probleme frecvente

| Simptom | Cauză probabilă | Fix |
|---|---|---|
| `Read error` constant | Lipsă pull-up rezistor 10kΩ | Adaugă rezistorul între DATA și VCC |
| `NaN` ocazional | Citire prea frecventă (< 2s) | Respectă `delay(2200)` |
| Temperatură incorectă ±2-3°C | Auto-încălzire prin VCC | Montează departe de surse de căldură |
| Nicio reacție | DATA și GND inversate sau VCC greșit | Verifică orientarea; re-citește pinout-ul |
| Erori după ore de funcționare | Condensare pe senzor | Normal în medii umede; usucă senzorul |

---

## Note pentru proiect MediTwin

DHT22 măsoară **temperatura și umiditatea camerei medicale**:
- Temperatura înaltă + aer viciat = risc critic ridicat
- Umiditatea >70%RH poate indica probleme de ventilație
- Intrare importantă în modelul AI (+30 la scor la temp critică)

**Atenție**: dacă BMP280 e disponibil, codul folosește **temperatura BMP280** în loc de DHT22 pentru temperatura ambientală (mai precisă). DHT22 rămâne ca sursă de umiditate.

Pin definit în `config.h`: `PIN_DHT22 = GPIO10`
