#!/usr/bin/env python3
"""
MediTwin AI — Raspberry Pi Monitor (cu HTTP Server)
=====================================================
Versiune cu server HTTP/REST pentru acces remote via Tailscale

Rulare:
  python3 monitor_http.py --port /dev/ttyACM0 --baud 115200 --http-host 0.0.0.0 --http-port 5000

Endpoints HTTP:
  GET  /status          → JSON cu starea curentă (citit din serial)
  POST /command         → trimite comando la ESP32 (fan, hc, alarm, reset)
  GET  /history        → ultimele 100 de lecturi
  GET  /logs            → log de comenzi

Acces remote (via Tailscale):
  curl http://<tailscale-ip>:5000/status
  curl -X POST http://<tailscale-ip>:5000/command -d '{"cmd":"FAN_ON"}'
"""

import argparse
import json
import logging
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field, asdict
from typing import Dict, Any
from http.server import HTTPServer, BaseHTTPRequestHandler
from urllib.parse import urlparse, parse_qs
import traceback

import serial  # pip3 install pyserial

log = logging.getLogger("MONITOR_HTTP")
logging.basicConfig(level=logging.INFO, format="[%(name)s] %(levelname)s: %(message)s")

# ─── Configuratie praguri ───────────────────────────────────────────────────
TEMP_WARNING_C   = 29.0
TEMP_CRITICAL_C  = 32.0
AIR_WARNING      = 2700
AIR_CRITICAL     = 3100
RISK_WARNING     = 25.0
RISK_CRITICAL    = 55.0
FAN_ON_RISK      = 40.0
FAN_OFF_RISK     = 20.0
TEMP_TREND_RISE  = 1.0

# ─── Stare globala (thread-safe) ────────────────────────────────────────────
@dataclass
class SensorReading:
    timestamp_ms: int = 0
    temperature_c: float = 0.0
    humidity_pct: float = 0.0
    air_quality_adc: int = 0
    light_level_adc: int = 0
    distance_cm: float = 0.0
    accel_g: float = 0.0
    gyro_x_rad_s: float = 0.0
    gyro_y_rad_s: float = 0.0
    gyro_z_rad_s: float = 0.0
    current_a: float = 0.0
    dht_ok: bool = False
    mpu_ok: bool = False
    fsm_state: str = "UNKNOWN"
    risk_score: float = 0.0

@dataclass
class GlobalState:
    latest_reading: SensorReading = field(default_factory=SensorReading)
    history: deque = field(default_factory=lambda: deque(maxlen=100))
    fan_manual_on: bool = False
    hc_enabled: bool = False
    alarm_enabled: bool = True
    command_history: deque = field(default_factory=lambda: deque(maxlen=50))
    lock: threading.Lock = field(default_factory=threading.Lock)

state = GlobalState()
serial_link = None
serial_lock = threading.Lock()


def send_serial_command(cmd: str) -> None:
    """Send a CMD token to ESP32 over serial."""
    global serial_link

    if serial_link is None:
        raise RuntimeError("Serial link is not ready")

    wire = f"CMD:{cmd}\n".encode("utf-8")
    with serial_lock:
        serial_link.write(wire)
        serial_link.flush()

# ─── HTTP Handler ──────────────────────────────────────────────────────────
class MonitorHTTPHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        """Handle GET requests"""
        path = urlparse(self.path).path
        
        try:
            if path == "/status":
                self.send_status_json()
            elif path == "/history":
                self.send_history_json()
            elif path == "/logs":
                self.send_logs_json()
            elif path == "/":
                self.send_dashboard_html()
            else:
                self.send_error(404, "Not Found")
        except Exception as e:
            log.error("GET %s error: %s", path, e)
            self.send_error(500, str(e))

    def do_POST(self):
        """Handle POST requests (command submission)"""
        path = urlparse(self.path).path
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length).decode("utf-8")
        
        try:
            if path == "/command":
                self.handle_command(body)
            else:
                self.send_error(404, "Not Found")
        except Exception as e:
            log.error("POST %s error: %s", path, e)
            self.send_error(500, str(e))

    def send_status_json(self):
        """Return current sensor reading as JSON"""
        with state.lock:
            data = {
                "ok": True,
                "reading": asdict(state.latest_reading),
                "system": {
                    "fan_manual_on": state.fan_manual_on,
                    "hc_enabled": state.hc_enabled,
                    "alarm_enabled": state.alarm_enabled,
                }
            }
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode("utf-8"))

    def send_history_json(self):
        """Return last 100 readings"""
        with state.lock:
            data = {
                "ok": True,
                "count": len(state.history),
                "readings": [asdict(r) for r in state.history]
            }
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode("utf-8"))

    def send_logs_json(self):
        """Return command history"""
        with state.lock:
            data = {
                "ok": True,
                "count": len(state.command_history),
                "commands": list(state.command_history)
            }
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data, indent=2).encode("utf-8"))

    def send_dashboard_html(self):
        """Simple HTML dashboard"""
        html = """
        <!DOCTYPE html>
        <html>
        <head>
            <title>MediTwin Monitor</title>
            <style>
                body { font-family: Arial; margin: 20px; background: #f5f5f5; }
                h1 { color: #333; }
                .status { background: white; padding: 15px; border-radius: 5px; margin: 10px 0; }
                .warning { color: orange; font-weight: bold; }
                .critical { color: red; font-weight: bold; }
                button { padding: 10px; margin: 5px; background: #007bff; color: white; border: none; border-radius: 3px; cursor: pointer; }
                button:hover { background: #0056b3; }
                pre { background: #f9f9f9; padding: 10px; overflow-x: auto; }
            </style>
            <script>
                function updateStatus() {
                    fetch('/status').then(r => r.json()).then(d => {
                        document.getElementById('status').innerText = JSON.stringify(d.reading, null, 2);
                        document.getElementById('system').innerText = JSON.stringify(d.system, null, 2);
                    });
                }
                function sendCmd(cmd) {
                    fetch('/command', { method: 'POST', body: JSON.stringify({cmd: cmd}) }).then(r => r.json()).then(d => {
                        console.log(d);
                        updateStatus();
                    });
                }
                setInterval(updateStatus, 2000);
                updateStatus();
            </script>
        </head>
        <body>
            <h1>MediTwin Monitor — Remote Tailscale</h1>
            <div class="status">
                <h2>Current Reading</h2>
                <pre id="status">Loading...</pre>
            </div>
            <div class="status">
                <h2>System State</h2>
                <pre id="system">Loading...</pre>
            </div>
            <div class="status">
                <h2>Controls</h2>
                <button onclick="sendCmd('FAN_ON')">Fan ON</button>
                <button onclick="sendCmd('FAN_OFF')">Fan OFF</button>
                <button onclick="sendCmd('HC_ON')">HC ON</button>
                <button onclick="sendCmd('HC_OFF')">HC OFF</button>
                <button onclick="sendCmd('ALARM_ON')">Alarm ON</button>
                <button onclick="sendCmd('ALARM_OFF')">Alarm OFF</button>
                <button onclick="sendCmd('FSM_RESET')">Reset FSM</button>
            </div>
        </body>
        </html>
        """
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.end_headers()
        self.wfile.write(html.encode("utf-8"))

    def handle_command(self, body: str):
        """Parse and execute command"""
        try:
            payload = json.loads(body)
            cmd = payload.get("cmd", "").upper()
            
            valid_cmds = ["FAN_ON", "FAN_OFF", "HC_ON", "HC_OFF", "ALARM_ON", "ALARM_OFF", "FSM_RESET"]
            if cmd not in valid_cmds:
                self.send_json_response({"ok": False, "error": f"Unknown command: {cmd}"}, 400)
                return
            
            # Send command to ESP32 (real control path).
            send_serial_command(cmd)

            # Keep high-level state in sync for /status.
            with state.lock:
                if cmd == "FAN_ON":
                    state.fan_manual_on = True
                elif cmd == "FAN_OFF":
                    state.fan_manual_on = False
                elif cmd == "HC_ON":
                    state.hc_enabled = True
                elif cmd == "HC_OFF":
                    state.hc_enabled = False
                elif cmd == "ALARM_ON":
                    state.alarm_enabled = True
                elif cmd == "ALARM_OFF":
                    state.alarm_enabled = False

                state.command_history.append({
                    "cmd": cmd,
                    "time": time.time(),
                    "status": "sent"
                })
            
            log.info("Command sent to ESP32: %s", cmd)
            
            self.send_json_response({"ok": True, "cmd": cmd, "message": "Command sent"})
            
        except json.JSONDecodeError:
            self.send_json_response({"ok": False, "error": "Invalid JSON"}, 400)
        except Exception as e:
            self.send_json_response({"ok": False, "error": str(e)}, 500)

    def send_json_response(self, data: Dict[str, Any], status: int = 200):
        """Send JSON response"""
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(data).encode("utf-8"))

    def log_message(self, format, *args):
        """Override to use our logger"""
        log.debug(format, *args)

# ─── Serial reader thread ──────────────────────────────────────────────────
def read_serial(ser: serial.Serial, stop_event: threading.Event):
    """Read telemetry stream from ESP32 serial."""

    try:
        while not stop_event.is_set():
            try:
                line = ser.readline().decode("utf-8").strip()
                if not line:
                    continue
                
                try:
                    obj = json.loads(line)
                    reading = SensorReading(
                        timestamp_ms=obj.get("timestamp_ms", 0),
                        temperature_c=obj.get("temperature_c", 0.0),
                        humidity_pct=obj.get("humidity_pct", 0.0),
                        air_quality_adc=obj.get("air_quality_adc", 0),
                        light_level_adc=obj.get("light_level_adc", 0),
                        distance_cm=obj.get("distance_cm", 0.0),
                        accel_g=obj.get("accel_g", 0.0),
                        gyro_x_rad_s=obj.get("gyro_x_rad_s", 0.0),
                        gyro_y_rad_s=obj.get("gyro_y_rad_s", 0.0),
                        gyro_z_rad_s=obj.get("gyro_z_rad_s", 0.0),
                        current_a=obj.get("current_a", 0.0),
                        dht_ok=obj.get("dht_ok", False),
                        mpu_ok=obj.get("mpu_ok", False),
                        fsm_state=obj.get("fsm_state", "UNKNOWN"),
                        risk_score=obj.get("risk_score", 0.0)
                    )
                    
                    with state.lock:
                        state.latest_reading = reading
                        state.history.append(reading)
                    
                    if reading.temperature_c >= TEMP_CRITICAL_C:
                        log.warning("🔴 CRITICAL: T=%.1f°C", reading.temperature_c)
                    elif reading.temperature_c >= TEMP_WARNING_C:
                        log.warning("🟡 WARNING: T=%.1f°C", reading.temperature_c)
                    
                except json.JSONDecodeError:
                    log.debug("Non-JSON: %s", line[:80])
            except Exception as e:
                log.error("Serial read error: %s", e)
                time.sleep(0.1)
    
    except KeyboardInterrupt:
        log.info("Serial reader stopped by user")
    finally:
        ser.close()

# ─── Main ──────────────────────────────────────────────────────────────────
def run(port: str, baud: int, http_host: str, http_port: int):
    """Main loop with HTTP server"""
    global serial_link

    try:
        serial_link = serial.Serial(port, baud, timeout=3.0, write_timeout=1.0)
        log.info("Serial opened: %s @ %d baud", port, baud)
    except Exception as e:
        log.error("Cannot open %s: %s", port, e)
        return

    stop_event = threading.Event()
    
    # Start serial reader thread
    serial_thread = threading.Thread(target=read_serial, args=(serial_link, stop_event), daemon=True)
    serial_thread.start()
    
    # Start HTTP server
    log.info("Starting HTTP server on %s:%d", http_host, http_port)
    server_address = (http_host, http_port)
    httpd = HTTPServer(server_address, MonitorHTTPHandler)
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        log.info("Shutting down...")
        stop_event.set()
        httpd.shutdown()
    finally:
        if serial_link is not None:
            try:
                serial_link.close()
            except Exception:
                pass
            serial_link = None

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MediTwin Monitor with HTTP Server")
    parser.add_argument("--port",  default="/dev/ttyACM0",
                        help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--baud",  type=int, default=115200,
                        help="Serial baud rate (default: 115200)")
    parser.add_argument("--http-host", default="0.0.0.0",
                        help="HTTP bind address (default: 0.0.0.0)")
    parser.add_argument("--http-port", type=int, default=5000,
                        help="HTTP port (default: 5000)")
    args = parser.parse_args()

    run(args.port, args.baud, args.http_host, args.http_port)
