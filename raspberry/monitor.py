#!/usr/bin/env python3
"""
MediTwin AI — Raspberry Pi Monitor
====================================
Primeste JSON de la ESP32 pe UART, decide starea FSM si trimite comenzi inapoi.

Protocol:
  ESP32 → Pi  (JSON line, la fiecare 2 s):
        {"timestamp_ms":12345,"temperature_c":25.30,"humidity_pct":60.5,
         "air_quality_adc":2450,"light_level_adc":1800,"distance_cm":45.2,
         "accel_g":1.02,"gyro_x_rad_s":0.01,"gyro_y_rad_s":0.00,
         "gyro_z_rad_s":0.00,"current_a":0.000,"dht_ok":true,
         "mpu_ok":true,"fsm_state":"SAFE","risk_score":5.0}

  Pi → ESP32  (CMD:<TOKEN>\n):
    CMD:FAN_ON  CMD:FAN_OFF          — control manual ventilator
    CMD:HC_ON   CMD:HC_OFF           — activare/dezactivare alarma HC-SR04
    CMD:ALARM_ON  CMD:ALARM_OFF      — alarma automata FSM
    CMD:FSM_RESET                    — recalibrare

Rulare:
  python3 raspberry/monitor.py --port /dev/ttyAMA0 --baud 115200
  python3 raspberry/monitor.py --port /dev/ttyUSB0 --baud 115200   # via USB-UART

Instalare dependinte:
  pip3 install pyserial
"""

import argparse
import json
import logging
import select
import socket
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Any, Optional

import serial  # pip3 install pyserial

# ─── Configuratie praguri ───────────────────────────────────────────────────
TEMP_WARNING_C   = 29.0    # daca temperatura > prag → WARNING
TEMP_CRITICAL_C  = 32.0
AIR_WARNING      = 2700    # ADC raw (fallback fara calibrare ESP32)
AIR_CRITICAL     = 3100
RISK_WARNING     = 25.0    # risk score (0-100)
RISK_CRITICAL    = 55.0
FAN_ON_RISK      = 40.0    # porneste fan cand risk depaseste pragul
FAN_OFF_RISK     = 20.0    # opreste fan cand risk scade sub prag
FAN_MANUAL_LOCK_S = 30     # durata FAN_ON manual (secunde)
TEMP_TREND_RISE  = 1.0     # °C/min — crestere rapida = WARNING
TEMP_HISTORY_LEN = 10      # numarul de masuratori pastrate pentru trend

# ─── State machine ──────────────────────────────────────────────────────────
class FsmState:
    SAFE     = "SAFE"
    WARNING  = "WARNING"
    CRITICAL = "CRITICAL"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s  %(levelname)-8s  %(message)s",
    datefmt="%H:%M:%S",
)
log = logging.getLogger("meditwin")


class TcpTransport:
    """Minimal serial-like wrapper peste un socket TCP pentru bridge-ul UART."""

    def __init__(self, host: str, port: int, timeout: float = 3.0):
        self._timeout = timeout
        self._rx = bytearray()
        self._sock = socket.create_connection((host, port), timeout=timeout)
        self._sock.settimeout(timeout)
        self._sock.setblocking(False)

    @property
    def in_waiting(self) -> int:
        self._drain_nonblocking()
        return len(self._rx)

    def _drain_nonblocking(self) -> None:
        while True:
            try:
                chunk = self._sock.recv(4096)
                if not chunk:
                    raise ConnectionError("Conexiune TCP inchisa de bridge")
                self._rx.extend(chunk)
                if len(chunk) < 4096:
                    return
            except BlockingIOError:
                return
            except socket.timeout:
                return

    def read(self, n: int = 1) -> bytes:
        deadline = time.time() + self._timeout
        while len(self._rx) < n:
            remaining = max(0.0, deadline - time.time())
            if remaining == 0.0:
                break
            readable, _, _ = select.select([self._sock], [], [], remaining)
            if not readable:
                break
            chunk = self._sock.recv(4096)
            if not chunk:
                raise ConnectionError("Conexiune TCP inchisa de bridge")
            self._rx.extend(chunk)
        if not self._rx:
            return b""
        take = min(n, len(self._rx))
        out = bytes(self._rx[:take])
        del self._rx[:take]
        return out

    def write(self, data: bytes) -> int:
        self._sock.sendall(data)
        return len(data)

    def flush(self) -> None:
        # sendall este deja sincron; metoda exista doar pentru compatibilitate.
        return

    def close(self) -> None:
        self._sock.close()


@dataclass
class MonitorState:
    fsm_state:       str   = FsmState.SAFE
    fan_on:          bool  = False
    alarm_on:        bool  = False
    hc_enabled:      bool  = False
    fan_locked_until: float = 0.0   # timestamp pana cand decide() nu atinge fan-ul
    # istoric temperatura (ts_s, temp_c)
    temp_history: deque = field(default_factory=lambda: deque(maxlen=TEMP_HISTORY_LEN))


def _temp_trend_c_per_min(history: deque) -> float:
    """Calculeaza panta temperaturii in °C/min pe ultimele masuratori."""
    if len(history) < 2:
        return 0.0
    oldest_ts, oldest_t = history[0]
    newest_ts, newest_t = history[-1]
    dt_min = (newest_ts - oldest_ts) / 60.0
    if dt_min < 0.05:
        return 0.0
    return (newest_t - oldest_t) / dt_min


def _first(data: dict, *keys, default=None):
    """Returneaza primul camp existent din lista (compatibilitate nou/vechi)."""
    for k in keys:
        if k in data:
            return data[k]
    return default


def decide(data: dict, state: MonitorState, ser: Any, lock: threading.Lock) -> None:
    """Aplica logica de decizie si trimite comenzi ESP32 daca e necesar."""
    now_s   = time.time()
    risk    = float(_first(data, "risk_score", "risk", default=0))
    temp    = float(_first(data, "temperature_c", "temp", default=0))
    air     = int(_first(data, "air_quality_adc", "air", default=0))
    dht_ok  = bool(_first(data, "dht_ok", "dht", default=0))
    esp_fsm = _first(data, "fsm_state", "state", default="SAFE")

    if dht_ok:
        state.temp_history.append((now_s, temp))

    trend = _temp_trend_c_per_min(state.temp_history)

    # ── Determinare stare noua ───────────────────────────────────────────────
    new_state = FsmState.SAFE

    if (risk >= RISK_CRITICAL
            or (dht_ok and temp >= TEMP_CRITICAL_C)
            or air >= AIR_CRITICAL
            or esp_fsm == FsmState.CRITICAL):
        new_state = FsmState.CRITICAL

    elif (risk >= RISK_WARNING
            or (dht_ok and temp >= TEMP_WARNING_C)
            or air >= AIR_WARNING
            or trend >= TEMP_TREND_RISE
            or esp_fsm == FsmState.WARNING):
        new_state = FsmState.WARNING

    # ── Tranzitii de stare ───────────────────────────────────────────────────
    if new_state != state.fsm_state:
        log.info("FSM: %s → %s  (risk=%.0f temp=%.1fC air=%d trend=%.2f°C/min)",
                 state.fsm_state, new_state, risk, temp, air, trend)
        state.fsm_state = new_state

        if new_state == FsmState.CRITICAL:
            _send(ser, "CMD:ALARM_ON", lock)
            if not state.alarm_on:
                state.alarm_on = True
        elif new_state == FsmState.WARNING:
            # la WARNING nu declanseaza alarma — doar logam
            pass
        else:  # SAFE
            if state.alarm_on:
                _send(ser, "CMD:ALARM_OFF", lock)
                state.alarm_on = False

    # ── Control fan independent de stare ────────────────────────────────────
    # Daca utilizatorul a dat manual FAN_ON, nu interfera pana expira lock-ul
    fan_locked = time.time() < state.fan_locked_until
    if not fan_locked:
        if risk >= FAN_ON_RISK and not state.fan_on:
            _send(ser, "CMD:FAN_ON", lock)
            state.fan_on = True
            log.info("Fan ON  (risk=%.0f)", risk)
        elif risk < FAN_OFF_RISK and state.fan_on:
            _send(ser, "CMD:FAN_OFF", lock)
            state.fan_on = False
            log.info("Fan OFF (risk=%.0f)", risk)

    # Log periodic status
    log.debug("  risk=%.0f  temp=%.1fC(trend%.2f)  air=%d  esp=%s  pi=%s  fan=%s",
              risk, temp, trend, air, esp_fsm, state.fsm_state,
              "ON" if state.fan_on else "OFF")


def _send(ser: Any, cmd: str, lock: threading.Lock) -> None:
    line = cmd + "\n"
    with lock:
        ser.write(line.encode())
    log.info("\u2192 Pi cmd: %s", cmd)


def _cli_thread(ser: serial.Serial, lock: threading.Lock, state: MonitorState) -> None:
    """Thread interactiv — citeste comenzi manuale de la tastatura (stdin)."""
    print("\n+-- Comenzi manuale disponiblile: -------------------+")
    print("|  +  sau FAN_ON   => porneste ventilatorul (30 s)   |")
    print("|  -  sau FAN_OFF  => opreste ventilatorul           |")
    print("|  h  sau HC       => toggle alarma HC-SR04 ON/OFF   |")
    print("|  q  sau QUIT     => opreste monitorul              |")
    print("+----------------------------------------------------+\n")

    for raw_line in sys.stdin:
        cmd_in = raw_line.strip().lower()
        if cmd_in in ("+", "fan_on", "fan+"):
            _send(ser, "CMD:FAN_ON", lock)
            state.fan_on = True
            state.fan_locked_until = time.time() + FAN_MANUAL_LOCK_S
            print(f"[CLI] FAN_ON trimis (blocat {FAN_MANUAL_LOCK_S}s fata de auto-control)")
        elif cmd_in in ("-", "fan_off", "fan-"):
            _send(ser, "CMD:FAN_OFF", lock)
            state.fan_on = False
            state.fan_locked_until = 0.0
            print("[CLI] FAN_OFF trimis")
        elif cmd_in in ("h", "hc", "hc_on", "hc_off"):
            if not state.hc_enabled:
                _send(ser, "CMD:HC_ON", lock)
                state.hc_enabled = True
                print("[CLI] HC_ON trimis — alarma HC-SR04 activa")
            else:
                _send(ser, "CMD:HC_OFF", lock)
                state.hc_enabled = False
                print("[CLI] HC_OFF trimis — alarma HC-SR04 dezactivata")
        elif cmd_in in ("q", "quit", "exit"):
            print("[CLI] Oprire monitor...")
            import os, signal
            os.kill(os.getpid(), signal.SIGINT)
            break
        elif cmd_in == "?" or cmd_in == "help":
            print("+  FAN_ON | -  FAN_OFF | h  HC toggle | q  quit")
        elif cmd_in:
            print(f"[CLI] Comanda necunoscuta: '{cmd_in}' (? = help)")


def _open_serial_transport(port: str, baud: int):
    log.info("MediTwin Monitor pornit pe serial %s @ %d baud", port, baud)
    ser = serial.Serial(port, baud, timeout=3.0, dsrdtr=False, rtscts=False)
    # Dezactiveaza DTR/RTS explicit — altfel ESP32 se reseteaza la conectare
    ser.dtr = False
    ser.rts = False
    log.info("Astept 5s ca ESP32 sa porneasca dupa conectare USB...")
    time.sleep(5)
    ser.reset_input_buffer()  # arunca orice date trimise in timpul bootului
    return ser


def _open_tcp_transport(host: str, port: int):
    log.info("MediTwin Monitor pornit pe TCP %s:%d", host, port)
    return TcpTransport(host, port, timeout=3.0)


def run(port: str, baud: int, tcp_host: Optional[str], tcp_port: int) -> None:
    try:
        if tcp_host:
            ser = _open_tcp_transport(tcp_host, tcp_port)
        else:
            ser = _open_serial_transport(port, baud)
    except (serial.SerialException, OSError, ConnectionError) as e:
        target = f"{tcp_host}:{tcp_port}" if tcp_host else port
        log.error("Nu pot deschide transportul %s: %s", target, e)
        sys.exit(1)

    state = MonitorState()
    lock  = threading.Lock()
    buf   = ""

    # Porneste thread interactiv CLI
    t = threading.Thread(target=_cli_thread, args=(ser, lock, state), daemon=True)
    t.start()

    try:
        while True:
            raw = ser.read(ser.in_waiting or 1).decode("utf-8", errors="replace")
            buf += raw
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    data = json.loads(line)
                    decide(data, state, ser, lock)
                except json.JSONDecodeError:
                    # Poate fi linie de debug de la ESP32 — ignora silentios
                    log.debug("Non-JSON: %s", line[:80])
    except KeyboardInterrupt:
        log.info("Oprit de utilizator.")
    finally:
        # Opreste alarma si fan la inchidere
        try:
            _send(ser, "CMD:ALARM_OFF", lock)
            _send(ser, "CMD:FAN_OFF", lock)
        except Exception:
            pass
        ser.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MediTwin AI — Raspberry Pi Monitor")
    parser.add_argument("--port",  default="/dev/ttyAMA0",
                        help="Port serial (default: /dev/ttyAMA0)")
    parser.add_argument("--baud",  type=int, default=115200,
                        help="Viteza UART (default: 115200)")
    parser.add_argument("--tcp-host", default=None,
                        help="IP/host bridge TCP (ex: 100.80.89.25)")
    parser.add_argument("--tcp-port", type=int, default=7000,
                        help="Port bridge TCP (default: 7000)")
    parser.add_argument("--verbose", "-v", action="store_true",
                        help="Afiseaza toate liniile JSON primite")
    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    run(args.port, args.baud, args.tcp_host, args.tcp_port)
