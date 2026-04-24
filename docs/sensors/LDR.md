# LDR — Light Dependent Resistor

## Ce este

Rezistor a cărui valoare se schimbă în funcție de lumină.  
- **Lumină multă** → rezistență mică → tensiune mare pe GPIO → ADC mare (~4095)  
- **Întuneric** → rezistență mare → tensiune mică pe GPIO → ADC mic (~0)

Tipul comun: **GL5516** (rezistență 5–10 kΩ la lumină, 1 MΩ la întuneric).

---

## Pini

| Pin fizic | Funcție |
|---|---|
| Picior 1 | Legat la 3V3 |
| Picior 2 | Semnal + rezistor pull-down la GND |

LDR-ul **nu are polaritate** — picioarele sunt interschimbabile.

---

## Schema circuitului

```
3V3 ──── [LDR] ──── GPIO0 ──── [10kΩ] ──── GND
```

Aceasta este o schemă de **divizor de tensiune**:
- LDR și rezistorul 10kΩ sunt în serie între 3V3 și GND
- GPIO0 citește tensiunea din **nodul de mijloc** (între LDR și rezistor)

---

## Montare pe breadboard — pas cu pas

```
Breadboard:
  Rând 10:  [LDR picior 1]  ←→  fir la 3V3 (ESP32)
  Rând 12:  [LDR picior 2]  ←→  fir la GPIO0 (ESP32)
  Rând 12:  [Rezistor 10kΩ picior 1]   (același rând cu picior 2 LDR)
  Rând 15:  [Rezistor 10kΩ picior 2]  ←→  fir la GND (ESP32)
```

**Vizual:**

```
  [3V3 rail]
       |
  [LDR]          ← orice orientare
       |
  ─────┬──────── fir GPIO0
       |
  [10kΩ]
       |
  [GND rail]
```

### Componente necesare
- 1× LDR (GL5516 sau similar)
- 1× rezistor 10 kΩ (maro-negru-portocaliu-auriu)
- 3× fire jumper

---

## Conexiuni ESP32-C6

| LDR circuit | GPIO ESP32-C6 |
|---|---|
| Nod mijloc (LDR + rezistor) | **GPIO 0** (ADC1_CH0) |
| Bara 3V3 | **3V3** |
| Bara GND | **GND** |

---

## Verificare funcționare

Uploadează `meditwin_test`, trimite `1` în Serial Monitor.

| Situație | Valoare ADC așteptată |
|---|---|
| Acoperi cu mâna | 0 – 400 |
| Lumină ambientală normală | 1500 – 2500 |
| Lanternă aproape | 3500 – 4095 |

Dacă valorile sunt **inversate** (acoperit = mare): ai pus rezistorul sus și LDR-ul jos — inversează-le.

---

## Calibrare

Nu necesită calibrare matematică. Calibrezi **pragurile** din `config.h` în funcție de mediu:

```c
// config.h — ajustează după ce testezi cu '0' (stability test)
#define THRESH_LIGHT_DIM    800    // sub asta = cameră întunecată
#define THRESH_LIGHT_BRIGHT 3000   // peste asta = iluminat puternic
```

Rulează testul de stabilitate (`0`) în condiții normale de cameră și notează valoarea medie. Setează `THRESH_LIGHT_DIM` la ~20% din acea valoare și `THRESH_LIGHT_BRIGHT` la ~80%.

---

## Probleme frecvente

| Simptom | Cauză probabilă | Fix |
|---|---|---|
| ADC mereu 4095 | Lipsă rezistor sau GND | Verifică rezistorul 10kΩ și firul GND |
| ADC mereu 0 | LDR nu e conectat la 3V3 | Verifică firul de la 3V3 |
| Valori care sar aleator | Conexiuni slabe în breadboard | Apasă componentele ferm în găuri |
| Nu reacționează la lumină | Pin greșit sau LDR defect | Verifică GPIO0; testează LDR cu multimetrul |
| Valori inversate | Rezistor sus, LDR jos | Inversează poziția |

---

## Note pentru proiect MediTwin

LDR-ul este folosit ca **context ambiental**:
- Cameră întunecată + temperatură mare = risc diferit față de cameră iluminată
- Poate detecta indirect prezența (lumina se aprinde când cineva intră)
- Intrare secundară în modelul AI (contribuție mică la scorul de risc)

Pin definit în `config.h`: `PIN_LDR = GPIO0`
