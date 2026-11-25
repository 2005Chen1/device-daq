# -*- coding: utf-8 -*-
"""上位机练习·模拟设备
每 100ms 往 TCP 连接上发一帧数据，每 27 帧故意发一个坏帧（CRC 错或截断）。

帧格式（小端）：
    帧头   2 字节   0xAA 0x55
    长度   2 字节   payload 的字节数
    数据   N 字节   4 路 int16 采样的模拟量
    校验   2 字节   CRC16-Modbus，算的是「长度 + 数据」这一段

用法：
    python sim_server.py            # 监听 127.0.0.1:9527
"""
import socket
import struct
import time

HOST = "127.0.0.1"
PORT = 9527
HEAD = b"\xAA\x55"
INTERVAL = 0.1
BAD_EVERY = 27


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def make_frame(seq: int) -> bytes:
    # 4 路通道，每路一个 int16，模拟电压/电流/温度/转速
    payload = struct.pack("<4h", seq % 1000, 2200 + seq % 50, 315, 1500 + seq % 200)
    body = struct.pack("<H", len(payload)) + payload
    return HEAD + body + struct.pack("<H", crc16_modbus(body))


def main() -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(1)
    print(f"[sim] listening on {HOST}:{PORT}", flush=True)

    conn, addr = srv.accept()
    print(f"[sim] client connected: {addr}", flush=True)

    seq = 0
    sent = 0
    try:
        while True:
            frame = make_frame(seq)
            seq += 1

            if seq % BAD_EVERY == 0:
                if seq % (BAD_EVERY * 2) == 0:
                    frame = frame[:-1]              # 截断：少一个校验字节
                else:
                    frame = frame[:-2] + b"\x00\x00"  # CRC 错
                print(f"[sim] seq={seq} sent BAD frame", flush=True)

            conn.sendall(frame)
            sent += 1
            time.sleep(INTERVAL)
    except (BrokenPipeError, ConnectionResetError):
        print(f"[sim] client gone, total sent={sent}", flush=True)
    finally:
        conn.close()
        srv.close()


if __name__ == "__main__":
    main()
