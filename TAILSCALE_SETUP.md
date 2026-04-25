# MediTwin Remote Access via Tailscale

## 1. Instalare Tailscale pe Raspberry Pi

```bash
# Instalare
curl -fsSL https://tailscale.com/install.sh | sh

# Conectare la rețea Tailscale
sudo tailscale up

# Verifică IP-ul Tailscale
sudo tailscale ip -4
```

## 2. Pornire Monitor HTTP

Pe Raspberry Pi, rulează:

```bash
python3 monitor_http.py --port /dev/ttyACM0 --baud 115200 --http-host 0.0.0.0 --http-port 5000
```

**Parametri:**
- `--port /dev/ttyACM0` — port USB de la ESP32 (sau `/dev/ttyAMA0` pentru UART GPIO)
- `--baud 115200` — viteza serial
- `--http-host 0.0.0.0` — ascultă pe TOATE interfețele (inclusiv Tailscale)
- `--http-port 5000` — port HTTP

## 3. Acces de pe Mașina Locală (via Tailscale)

Obțin IP-ul Tailscale al Raspberry Pi:

```bash
# Pe Raspberry:
sudo tailscale ip -4
# Exemplu: 100.85.123.45
```

Accesez din browser sau cu `curl`:

```bash
# Dashboard web
http://100.85.123.45:5000

# API: Status curent
curl http://100.85.123.45:5000/status | jq

# API: Istoric (ultimele 100 lecturi)
curl http://100.85.123.45:5000/history | jq

# API: Comenzi
curl -X POST http://100.85.123.45:5000/command \
  -H "Content-Type: application/json" \
  -d '{"cmd":"FAN_ON"}'
```

## 4. Comenzi disponibile

- `FAN_ON` / `FAN_OFF` — control ventilator
- `HC_ON` / `HC_OFF` — alarma HC-SR04
- `ALARM_ON` / `ALARM_OFF` — alarma système
- `FSM_RESET` — recalibrare senzori

## 5. Exemplu cURL pe Windows

```powershell
$url = "http://100.85.123.45:5000"

# Status
Invoke-WebRequest -Uri "$url/status" | Select-Object -ExpandProperty Content | ConvertFrom-Json

# Trimite comandă
$body = @{"cmd"="FAN_ON"} | ConvertTo-Json
Invoke-WebRequest -Uri "$url/command" -Method POST -Body $body -ContentType "application/json"
```

## 6. Troubleshooting

### Nu se conectează la Tailscale IP?

```bash
# Verifică dacă Tailscale e activ
sudo systemctl status tailscaled

# Verifică firewall
sudo iptables -L | grep 5000
# (Ar trebui să NU fie blocat; Tailscale uses its own interface)
```

### Nu vede datele?

```bash
# Verifică seriile
ls -la /dev/ttyACM0 /dev/ttyAMA0

# Test serial direct
python3 -c "import serial; s=serial.Serial('/dev/ttyACM0', 115200); print(s.readline())"
```

### Portul 5000 e ocupat?

```bash
# Folosește alt port
python3 monitor_http.py --http-port 8080
```

## 7. Setare Systemd (autostart pe Raspberry)

Creeaza `/etc/systemd/system/meditwin-monitor.service`:

```ini
[Unit]
Description=MediTwin Monitor HTTP Server
After=network.target tailscaled.service

[Service]
Type=simple
User=pi
WorkingDirectory=/home/pi/MediTwin2026
ExecStart=/usr/bin/python3 /home/pi/MediTwin2026/raspberry/monitor_http.py \
  --port /dev/ttyACM0 --baud 115200 --http-host 0.0.0.0 --http-port 5000
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
```

Activare:

```bash
sudo systemctl daemon-reload
sudo systemctl enable meditwin-monitor
sudo systemctl start meditwin-monitor
sudo systemctl status meditwin-monitor
```

## 8. UART-over-TCP Bridge (Windows <-> Raspberry)

Cand ESP32 e conectat la laptop (COM) si Raspberry trebuie sa citeasca telemetria prin retea:

Pe Windows (masina cu ESP32 pe USB):

```powershell
python serial_bridge.py --serial-port COM8 --baud 115200 --host 0.0.0.0 --tcp-port 7000
```

Pe Raspberry (prin Tailscale):

```bash
python3 raspberry/monitor.py --tcp-host <WINDOWS_TAILSCALE_IP> --tcp-port 7000 -v
```

Verificare conectivitate simpla din Raspberry:

```bash
nc -vz -w 3 <WINDOWS_TAILSCALE_IP> 7000
```

Daca apare `no matching peer` la `tailscale status`, IP-ul folosit nu apartine niciunui peer activ. Foloseste IP-ul real din:

```bash
tailscale status
tailscale ping <WINDOWS_HOSTNAME>
```

Pentru timeout pe `nc`, verifica si firewall-ul Windows pentru portul TCP 7000 (Inbound rule).
