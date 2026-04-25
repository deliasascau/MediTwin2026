import argparse
import socket
import threading
import time

import serial


def serial_to_socket(ser, conn, stop_event):
    try:
        while not stop_event.is_set():
            data = ser.read(ser.in_waiting or 1)
            if data:
                conn.sendall(data)
    except Exception as e:
        print("[BRIDGE] serial_to_socket stopped:", e)
    finally:
        stop_event.set()


def socket_to_serial(ser, conn, stop_event):
    try:
        while not stop_event.is_set():
            data = conn.recv(1024)
            if not data:
                break
            ser.write(data)
            ser.flush()
            text = data.decode(errors="replace").strip()
            if text:
                print("[BRIDGE] Pi -> ESP:", text)
    except Exception as e:
        print("[BRIDGE] socket_to_serial stopped:", e)
    finally:
        stop_event.set()


def main():
    parser = argparse.ArgumentParser(description="MediTwin serial <-> TCP bridge")
    parser.add_argument("--serial-port", default="COM8", help="Port serial ESP32 (default: COM8)")
    parser.add_argument("--baud", type=int, default=115200, help="Baud serial (default: 115200)")
    parser.add_argument("--host", default="0.0.0.0", help="TCP bind host (default: 0.0.0.0)")
    parser.add_argument("--tcp-port", type=int, default=7000, help="TCP bind port (default: 7000)")
    args = parser.parse_args()

    print(f"[BRIDGE] Opening serial {args.serial_port} @ {args.baud}")
    ser = serial.Serial(args.serial_port, args.baud, timeout=0.2)
    time.sleep(2)

    print(f"[BRIDGE] Listening on TCP {args.host}:{args.tcp_port}")
    print(f"[BRIDGE] Quick test from Pi: nc -vz -w 3 <bridge-ip> {args.tcp_port}")
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((args.host, args.tcp_port))
    server.listen(1)

    while True:
        print("[BRIDGE] Waiting for Raspberry connection...")
        conn, addr = server.accept()
        conn.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
        print("[BRIDGE] Raspberry connected:", addr)

        stop_event = threading.Event()
        t1 = threading.Thread(target=serial_to_socket, args=(ser, conn, stop_event), daemon=True)
        t2 = threading.Thread(target=socket_to_serial, args=(ser, conn, stop_event), daemon=True)

        t1.start()
        t2.start()

        while not stop_event.is_set():
            time.sleep(0.1)

        try:
            conn.close()
        except Exception:
            pass

        print("[BRIDGE] Raspberry disconnected. Waiting again...")


if __name__ == "__main__":
    main()