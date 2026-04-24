#!/usr/bin/env python3
"""
MediTwin2026 - Raspberry Pi Monitor

Role:
- receives JSON telemetry from ESP32 over USB serial;
- decides SAFE / WARNING / CRITICAL state;
- sends commands back to ESP32:
    CMD:FAN_ON
    CMD:FAN_OFF
    CMD:HC_ON
    CMD:HC_OFF
    CMD:ALARM_ON
    CMD:ALARM_OFF
    CMD:FSM_RESET

Recommended run command on Raspberry Pi:
    python3 raspberry/monitor.py --port /dev/ttyACM0 --baud 115200

Install:
    pip3 install pyserial
"""

import argparse
import json
import logging
import os
import signal
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from typing import Any, Optional

import serial
from serial.tools import list_ports


# =========================
# Raspberry decision limits
# =========================

TEMP_WARNING_C = 29.0
TEMP_CRITICAL_C = 32.0

AIR_WARNING = 2700
AIR_CRITICAL = 3100

RISK_WARNING = 25.0
RISK_CRITICAL = 55.0

FAN_ON_RISK = 40.0
FAN_OFF_RISK = 20.0

TEMP_TREND_RISE = 1.0          # degrees C / minute
TEMP_HISTORY_LEN = 10

MANUAL_FAN_LOCK_S = 30.0       # after manual FAN_ON, auto-control is locked
SERIAL_TIMEOUT_S = 1.0
NO_DATA_WARNING_S = 10.0


class FsmState:
    SAFE = "SAFE"
    WARNING = "WARNING"
    CRITICAL = "CRITICAL"


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s %(levelname)-8s %(message)s",
    datefmt="%H:%M:%S",
)

log = logging.getLogger("meditwin")


@dataclass
class MonitorState:
    fsm_state: str = FsmState.SAFE
    fan_on: bool = False
    alarm_on: bool = False
    hc_enabled: bool = True
    fan_locked_until: float = 0.0
    last_packet_time: float = 0.0
    packets_ok: int = 0
    packets_bad: int = 0
    temp_history: deque = field(default_factory=lambda: deque(maxlen=TEMP_HISTORY_LEN))


def list_serial_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    print("Detected serial ports:")
    for p in ports:
        print(f"  {p.device} - {p.description}")


def open_serial(port: str, baud: int) -> serial.Serial:
    try:
        ser = serial.Serial(
            port=port,
            baudrate=baud,
            timeout=SERIAL_TIMEOUT_S,
            write_timeout=SERIAL_TIMEOUT_S,
        )

        # ESP32-C6 USB CDC may reset on port open.
        time.sleep(2.0)

        ser.reset_input_buffer()
        ser.reset_output_buffer()

        log.info("Connected to ESP on %s @ %d baud", port, baud)
        return ser

    except serial.SerialException as exc:
        log.error("Cannot open serial port %s: %s", port, exc)
        log.error("Try checking: /dev/ttyACM* and /dev/ttyUSB*")
        sys.exit(1)


def send_cmd(ser: serial.Serial, lock: threading.Lock, cmd: str) -> bool:
    """Send a command to ESP using the exact wire format CMD:<TOKEN>\\n."""
    if not cmd.startswith("CMD:"):
        cmd = "CMD:" + cmd

    line = cmd.strip() + "\n"

    try:
        with lock:
            ser.write(line.encode("utf-8"))
            ser.flush()

        log.info("Pi -> ESP: %s", cmd)
        return True

    except serial.SerialException as exc:
        log.error("Failed sending %s: %s", cmd, exc)
        return False


def temp_trend_c_per_min(history: deque) -> float:
    if len(history) < 2:
        return 0.0

    old_ts, old_temp = history[0]
    new_ts, new_temp = history[-1]

    dt_min = (new_ts - old_ts) / 60.0
    if dt_min <= 0.05:
        return 0.0

    return (new_temp - old_temp) / dt_min


def first_key(data: dict, keys: tuple[str, ...], default: Any = 0) -> Any:
    for key in keys:
        if key in data:
            return data[key]
    return default


def safe_float(data: dict, keys: tuple[str, ...], default: float = 0.0) -> float:
    try:
        value = first_key(data, keys, default)
        if value is None:
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def safe_int(data: dict, keys: tuple[str, ...], default: int = 0) -> int:
    try:
        value = first_key(data, keys, default)
        if value is None:
            return default
        return int(float(value))
    except (TypeError, ValueError):
        return default


def safe_bool(data: dict, keys: tuple[str, ...], default: bool = False) -> bool:
    value = first_key(data, keys, default)
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        v = value.strip().lower()
        return v in ("1", "true", "yes", "on")
    return bool(value)


def normalize_esp_state(value: Optional[str]) -> str:
    if not value:
        return FsmState.SAFE

    state = str(value).upper().strip()
    if state in (FsmState.SAFE, FsmState.WARNING, FsmState.CRITICAL):
        return state
    return FsmState.SAFE


def decide(data: dict, state: MonitorState, ser: serial.Serial, lock: threading.Lock) -> None:
    now = time.time()

    # Support both clean schema and legacy short keys.
    risk = safe_float(data, ("risk_score", "risk"), 0.0)
    temp = safe_float(data, ("temperature_c", "temp"), 0.0)
    air = safe_int(data, ("air_quality_adc", "air"), 0)
    dist = safe_float(data, ("distance_cm", "dist"), -1.0)
    dht_ok = safe_bool(data, ("dht_ok", "dht"), False)
    esp_state = normalize_esp_state(first_key(data, ("fsm_state", "state"), FsmState.SAFE))

    if dht_ok:
        state.temp_history.append((now, temp))

    trend = temp_trend_c_per_min(state.temp_history)

    new_state = FsmState.SAFE

    if (
        risk >= RISK_CRITICAL
        or air >= AIR_CRITICAL
        or esp_state == FsmState.CRITICAL
        or (dht_ok and temp >= TEMP_CRITICAL_C)
    ):
        new_state = FsmState.CRITICAL

    elif (
        risk >= RISK_WARNING
        or air >= AIR_WARNING
        or esp_state == FsmState.WARNING
        or (dht_ok and temp >= TEMP_WARNING_C)
        or trend >= TEMP_TREND_RISE
    ):
        new_state = FsmState.WARNING

    if new_state != state.fsm_state:
        log.info(
            "FSM Pi: %s -> %s | ESP=%s risk=%.1f temp=%.1fC air=%d dist=%.1f trend=%.2fC/min",
            state.fsm_state,
            new_state,
            esp_state,
            risk,
            temp,
            air,
            dist,
            trend,
        )
        state.fsm_state = new_state

    if state.fsm_state == FsmState.CRITICAL:
        if not state.alarm_on and send_cmd(ser, lock, "CMD:ALARM_ON"):
            state.alarm_on = True
    elif state.fsm_state == FsmState.SAFE:
        if state.alarm_on and send_cmd(ser, lock, "CMD:ALARM_OFF"):
            state.alarm_on = False

    fan_locked = time.time() < state.fan_locked_until
    if not fan_locked:
        if risk >= FAN_ON_RISK and not state.fan_on:
            if send_cmd(ser, lock, "CMD:FAN_ON"):
                state.fan_on = True
                log.info("Fan AUTO ON | risk=%.1f", risk)
        elif risk < FAN_OFF_RISK and state.fan_on:
            if send_cmd(ser, lock, "CMD:FAN_OFF"):
                state.fan_on = False
                log.info("Fan AUTO OFF | risk=%.1f", risk)

    log.debug(
        "RX ok=%d bad=%d | Pi=%s ESP=%s risk=%.1f temp=%.1f air=%d dist=%.1f fan=%s alarm=%s hc=%s",
        state.packets_ok,
        state.packets_bad,
        state.fsm_state,
        esp_state,
        risk,
        temp,
        air,
        dist,
        "ON" if state.fan_on else "OFF",
        "ON" if state.alarm_on else "OFF",
        "ON" if state.hc_enabled else "OFF",
    )


def cli_thread(ser: serial.Serial, lock: threading.Lock, state: MonitorState) -> None:
    print()
    print("+------------------- Manual commands -------------------+")
    print("| fan on / +        -> turn fan ON                      |")
    print("| fan off / -       -> turn fan OFF                     |")
    print("| hc on             -> enable HC-SR04 alarm logic       |")
    print("| hc off            -> disable HC-SR04 alarm logic      |")
    print("| alarm on          -> force buzzer/alarm ON            |")
    print("| alarm off         -> force buzzer/alarm OFF           |")
    print("| reset             -> FSM reset on ESP                 |")
    print("| status            -> print local Pi state             |")
    print("| q                 -> quit                              |")
    print("+--------------------------------------------------------+")
    print()

    for raw in sys.stdin:
        cmd = raw.strip().lower()

        if cmd in ("fan on", "fan_on", "+"):
            if send_cmd(ser, lock, "CMD:FAN_ON"):
                state.fan_on = True
                state.fan_locked_until = time.time() + MANUAL_FAN_LOCK_S
                print(f"[CLI] FAN_ON sent. Auto-control locked for {MANUAL_FAN_LOCK_S:.0f}s.")

        elif cmd in ("fan off", "fan_off", "-"):
            if send_cmd(ser, lock, "CMD:FAN_OFF"):
                state.fan_on = False
                state.fan_locked_until = 0.0
                print("[CLI] FAN_OFF sent.")

        elif cmd in ("hc on", "hc_on"):
            if send_cmd(ser, lock, "CMD:HC_ON"):
                state.hc_enabled = True
                print("[CLI] HC_ON sent.")

        elif cmd in ("hc off", "hc_off"):
            if send_cmd(ser, lock, "CMD:HC_OFF"):
                state.hc_enabled = False
                print("[CLI] HC_OFF sent.")

        elif cmd in ("hc", "h"):
            if state.hc_enabled:
                if send_cmd(ser, lock, "CMD:HC_OFF"):
                    state.hc_enabled = False
                    print("[CLI] HC_OFF sent.")
            else:
                if send_cmd(ser, lock, "CMD:HC_ON"):
                    state.hc_enabled = True
                    print("[CLI] HC_ON sent.")

        elif cmd in ("alarm on", "alarm_on"):
            if send_cmd(ser, lock, "CMD:ALARM_ON"):
                state.alarm_on = True
                print("[CLI] ALARM_ON sent.")

        elif cmd in ("alarm off", "alarm_off"):
            if send_cmd(ser, lock, "CMD:ALARM_OFF"):
                state.alarm_on = False
                print("[CLI] ALARM_OFF sent.")

        elif cmd in ("reset", "fsm reset", "fsm_reset"):
            send_cmd(ser, lock, "CMD:FSM_RESET")
            print("[CLI] FSM_RESET sent.")

        elif cmd == "status":
            print(
                f"[STATUS] Pi={state.fsm_state}, fan={state.fan_on}, "
                f"alarm={state.alarm_on}, hc={state.hc_enabled}, "
                f"rx_ok={state.packets_ok}, rx_bad={state.packets_bad}"
            )

        elif cmd in ("q", "quit", "exit"):
            print("[CLI] Stopping monitor...")
            os.kill(os.getpid(), signal.SIGINT)
            break

        elif cmd in ("help", "?"):
            print("Commands: fan on, fan off, hc on, hc off, alarm on, alarm off, reset, status, q")

        elif cmd:
            print(f"[CLI] Unknown command: {cmd}")


def handle_line(line: str, state: MonitorState, ser: serial.Serial, lock: threading.Lock, verbose: bool) -> None:
    line = line.strip()

    if not line:
        return

    # ESP sends debug text over same serial; telemetry lines are JSON objects.
    if not line.startswith("{"):
        if verbose:
            log.info("ESP debug: %s", line)
        return

    try:
        data = json.loads(line)
    except json.JSONDecodeError:
        state.packets_bad += 1
        log.warning("Invalid JSON from ESP: %s", line[:160])
        return

    state.packets_ok += 1
    state.last_packet_time = time.time()

    if verbose:
        log.info("ESP -> Pi JSON: %s", data)

    decide(data, state, ser, lock)


def run(port: str, baud: int, verbose: bool) -> None:
    ser = open_serial(port, baud)
    lock = threading.Lock()
    state = MonitorState()

    t = threading.Thread(target=cli_thread, args=(ser, lock, state), daemon=True)
    t.start()

    buffer = ""
    last_no_data_warning = 0.0

    try:
        while True:
            try:
                raw = ser.read(ser.in_waiting or 1)
            except serial.SerialException as exc:
                log.error("Serial connection lost: %s", exc)
                break

            if raw:
                text = raw.decode("utf-8", errors="replace")
                buffer += text

                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    handle_line(line, state, ser, lock, verbose)

            else:
                now = time.time()
                if state.last_packet_time > 0 and now - state.last_packet_time > NO_DATA_WARNING_S:
                    if now - last_no_data_warning > NO_DATA_WARNING_S:
                        log.warning(
                            "No telemetry JSON received for %.0fs. Check port/cable/baud.",
                            now - state.last_packet_time,
                        )
                        last_no_data_warning = now

    except KeyboardInterrupt:
        log.info("Stopped by user.")

    finally:
        log.info("Safe shutdown: turning alarm and fan OFF.")
        try:
            send_cmd(ser, lock, "CMD:ALARM_OFF")
            send_cmd(ser, lock, "CMD:FAN_OFF")
        except Exception:
            pass

        try:
            ser.close()
        except Exception:
            pass


def main() -> None:
    parser = argparse.ArgumentParser(description="MediTwin2026 Raspberry Pi USB monitor")
    parser.add_argument("--port", default="/dev/ttyACM0", help="ESP32 USB CDC serial port, usually /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baudrate")
    parser.add_argument("--verbose", "-v", action="store_true", help="Print debug lines and JSON packets")
    parser.add_argument("--list-ports", action="store_true", help="List available serial ports and exit")

    args = parser.parse_args()

    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    if args.list_ports:
        list_serial_ports()
        return

    run(args.port, args.baud, args.verbose)


if __name__ == "__main__":
    main()
