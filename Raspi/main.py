"""
main.py — FastAPI real-time EEG server for MonEEE CM5.

Architecture (mirrors main_test_filter.py):
  - Reader thread:  reads C process stdout, parses hex → µV, accumulates blocks
  - Filter task:    async consumer — filters blocks, broadcasts binary via WebSocket
  - Status task:    async 1 Hz — broadcasts JSON system metrics via /ws/status

WebSocket binary format (EEG data on /ws):
  [4-byte uint32 block_num LE] + [float32 planar: CH0×N, CH1×N, ..., CH31×N]

WebSocket JSON format (status on /ws/status):
  {"cpu": 12.3, "ram": 45.6, "pkt_loss": 0, "state": "EEG", "block_num": 42}
"""

import asyncio
from contextlib import asynccontextmanager
import struct
import subprocess
import threading
import sys
import os
import time
import numpy as np
from scipy.signal import butter, iirnotch, sosfilt, sosfilt_zi, tf2sos

from fastapi import FastAPI, WebSocket, WebSocketDisconnect, Request
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates
from fastapi.responses import HTMLResponse
from pydantic import BaseModel
import uvicorn
import json

# ─────────────────────────────────────────────────────────
# ADS1299 parameters
# ─────────────────────────────────────────────────────────
FS         = 250                         # Sampling frequency (Hz)
GAIN       = 24                          # PGA gain
VREF       = 4.5                         # Reference voltage (V)
NUM_CH     = 32                          # Total channels (4 × ADS1299 × 8)
BLOCK_SIZE = FS // 5                     # 50 samples (200 ms latency)
LSB_UV     = (VREF / (2**23 - 1) / GAIN) * 1e6   # µV per LSB (EEG, gain=24)
LSB_UV_IMP = (VREF / (2**23 - 1) / 1) * 1e6       # µV per LSB (IMP, gain=1)

# ─────────────────────────────────────────────────────────
# IPC commands (must match main.c defines)
# ─────────────────────────────────────────────────────────
CMD_START_EEG = b'\x01'
CMD_STOP      = b'\x02'
CMD_START_IMP = b'\x03'

# ─────────────────────────────────────────────────────────
# Filter design (computed once at import time)
# ─────────────────────────────────────────────────────────
# 4th-order Butterworth bandpass 5–50 Hz
_sos_bp = butter(4, [5.0, 50.0], btype='bandpass', fs=FS, output='sos')

# 60 Hz notch filter (Q = 30)
_b_notch, _a_notch = iirnotch(60.0, 30.0, fs=FS)
_sos_notch = tf2sos(_b_notch, _a_notch)

# Cascade: bandpass then notch
SOS_ALL = np.vstack([_sos_bp, _sos_notch])


# ═══════════════════════════════════════════════════════════
# Signal processing
# ═══════════════════════════════════════════════════════════

class RealtimeFilter:
    """
    Maintains per-channel IIR filter state across successive blocks
    so the output is continuous (no transient at block boundaries).
    """

    def __init__(self):
        self._zi = None

    def reset(self):
        self._zi = None

    def process(self, block):
        """
        Filter a (BLOCK_SIZE, NUM_CH) array.
        Returns the filtered array (same shape).
        """
        if self._zi is None:
            zi_proto = sosfilt_zi(SOS_ALL)                   # (n_sections, 2)
            self._zi = np.tile(zi_proto[:, :, np.newaxis],
                               (1, 1, NUM_CH))               # (n_sections, 2, 32)
            # Scale by first sample to reduce startup transient
            self._zi = self._zi * block[0, :]

        filtered, self._zi = sosfilt(SOS_ALL, block, axis=0, zi=self._zi)
        return filtered


def hex24_to_signed(h):
    """Convert a 6-char hex string to a signed 24-bit integer."""
    v = int(h, 16)
    return v - 0x1000000 if v >= 0x800000 else v


def hex24_to_signed_vec(hex_strings):
    """Vectorized: convert a list of 6-char hex strings to signed 24-bit integers.
    Returns a NumPy float64 array. ~30× faster than calling hex24_to_signed() in a loop."""
    vals = np.array([int(h, 16) for h in hex_strings], dtype=np.int32)
    vals[vals >= 0x800000] -= 0x1000000
    return vals.astype(np.float64)


# ═══════════════════════════════════════════════════════════
# FastAPI & WebSocket Managers
# ═══════════════════════════════════════════════════════════

@asynccontextmanager
async def lifespan(app):
    """Modern FastAPI lifecycle: replaces deprecated on_event."""
    loop = asyncio.get_running_loop()
    mon_process.start(loop)
    yield
    mon_process.shutdown()


app = FastAPI(title="MonEEE CM5 - Real-Time EEG", lifespan=lifespan)
templates = Jinja2Templates(directory="templates")
app.mount("/static", StaticFiles(directory="static"), name="static")


class ConnectionManager:
    """Manages a set of WebSocket connections for binary EEG data."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast_bytes(self, data: bytes):
        for connection in list(self.active_connections):
            try:
                await connection.send_bytes(data)
            except Exception:
                self.disconnect(connection)


class StatusManager:
    """Manages WebSocket connections for JSON status updates."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

    async def broadcast_json(self, data: dict):
        msg = json.dumps(data)
        for connection in list(self.active_connections):
            try:
                await connection.send_text(msg)
            except Exception:
                self.disconnect(connection)


manager = ConnectionManager()
status_manager = StatusManager()


# ═══════════════════════════════════════════════════════════
# C Process Manager
# ═══════════════════════════════════════════════════════════

class MonEEEProcess:
    def __init__(self):
        self.process = None
        self.current_state = "IDLE"
        self.eeg_buffer = []
        self.data_queue = None          # asyncio.Queue for EEG blocks
        self.loop = None
        self.reader_thread = None
        self.stop_event = threading.Event()

        # System metrics (parsed from C process CSV tail)
        self.cpu = 0.0
        self.ram = 0.0
        self.pkt_loss = 0
        self.imp_values = None          # list[float] of 32 impedance values (Ω) from last IMP sweep
        self.block_num = 0              # monotonic block counter

    def start(self, loop):
        self.loop = loop
        self.data_queue = asyncio.Queue(maxsize=8)
        executable = "./moneee"

        if not os.path.exists(executable):
            print(f"[ERROR] Executable '{executable}' not found.")
            sys.exit(1)

        print(f"[MAIN] Starting process {executable}...")
        self.process = subprocess.Popen(
            [executable],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            bufsize=0,
        )

        self.reader_thread = threading.Thread(
            target=self._read_output, daemon=True
        )
        self.reader_thread.start()

        # Start the async consumer tasks
        asyncio.create_task(self._filter_and_broadcast())
        asyncio.create_task(self._status_broadcast_loop())

    # ─────────────────────────────────────────────────────
    # IPC: send command to C process
    # ─────────────────────────────────────────────────────
    def send_command(self, cmd_byte):
        if not self.process:
            return
        try:
            self.process.stdin.write(cmd_byte)
            self.process.stdin.flush()
        except Exception as e:
            print(f"[ERROR] IPC Send Failed: {e}")

    # ─────────────────────────────────────────────────────
    # Reader thread: C stdout → parse → enqueue blocks
    # ─────────────────────────────────────────────────────
    def _read_output(self):
        eeg_buffer = []
        usb_buffer = []

        try:
            for line in iter(self.process.stdout.readline, b''):
                if not line or self.stop_event.is_set():
                    break
                decoded = line.decode('utf-8', errors='replace')

                # ── State transition detection (stop/auto-stop) ──
                if "Impedance sweep complete" in decoded or \
                   "[CMD] Acquisition auto-stopped" in decoded:
                    self.current_state = "IDLE"
                    print(decoded, end='')
                    continue

                if "[CMD] Acquisition stopped." in decoded:
                    self.current_state = "IDLE"
                    eeg_buffer.clear()
                    usb_buffer.clear()
                    self._safe_enqueue("RESET")
                    print(decoded, end='')
                    continue

                if "No acquisition in progress" in decoded:
                    self.current_state = "IDLE"
                    print(decoded, end='')
                    continue

                # ── Mode-start confirmations (fix race with delayed stop) ──
                if "[CMD] Mode EEG started." in decoded:
                    self.current_state = "EEG"
                    eeg_buffer.clear()
                    usb_buffer.clear()
                    print(decoded, end='')
                    continue
                if "[CMD] Mode IMP started." in decoded or \
                   "Mode IMP started" in decoded:
                    self.current_state = "IMP"
                    print(decoded, end='')
                    continue

                # ── Log lines (start with '[') → print directly ──
                if decoded.lstrip().startswith('['):
                    print(decoded, end='')
                    continue

                # ── Data lines (CSV) ──
                parts = decoded.strip().split(',')
                if len(parts) < NUM_CH:
                    print(decoded, end='')  # not a valid data line
                    continue

                # ── Parse system metrics from CSV tail ──
                # C format: 32 CH hex, 4 USB events, cpu, ram, lost_packets
                # That's NUM_CH + 4 + 3 = 39 fields
                try:
                    self.cpu = float(parts[NUM_CH + 4])
                    self.ram = float(parts[NUM_CH + 5])
                    self.pkt_loss = int(parts[NUM_CH + 6])
                except (IndexError, ValueError):
                    pass  # not enough fields or malformed

                if self.current_state == "IMP":
                    print(decoded, end='')
                    try:
                        raw_ints = hex24_to_signed_vec(parts[:NUM_CH])
                        raw = ((raw_ints * LSB_UV_IMP) / 12) - 2200
                        self.imp_values = raw.tolist()  # impedance in Ω
                        # IMP is one-shot: go back to IDLE after receiving data
                        self.current_state = "IDLE"
                        print("[PYTHON] Impedance data received, returning to IDLE")
                    except (ValueError, IndexError):
                        pass

                elif self.current_state == "EEG":
                    try:
                        raw = hex24_to_signed_vec(parts[:NUM_CH])
                        raw *= LSB_UV
                        eeg_buffer.append(raw)

                        usb_raw = np.array(
                            [int(parts[NUM_CH + i], 16) for i in range(4)],
                            dtype=np.float64
                        )
                        usb_buffer.append(usb_raw)

                        # When we have a full block, enqueue for filtering
                        if len(eeg_buffer) >= BLOCK_SIZE:
                            block_eeg = np.array(eeg_buffer[:BLOCK_SIZE])  # (50, 32)
                            block_usb = np.array(usb_buffer[:BLOCK_SIZE])  # (50, 4)
                            eeg_buffer = eeg_buffer[BLOCK_SIZE:]
                            usb_buffer = usb_buffer[BLOCK_SIZE:]
                            self._safe_enqueue((block_eeg, block_usb))
                    except (ValueError, IndexError):
                        pass  # malformed line

                else:
                    print(decoded, end='')

        except Exception as e:
            print(f"[ERROR] Reader thread: {e}")

    # ─────────────────────────────────────────────────────
    # Safe enqueue: non-blocking put, drop oldest on overflow
    # ─────────────────────────────────────────────────────
    def _safe_enqueue(self, item):
        """Thread-safe enqueue into the asyncio Queue.
        Drops the oldest item if the queue is full to prevent deadlock."""
        if not self.loop or not self.data_queue:
            return

        def _put():
            try:
                self.data_queue.put_nowait(item)
            except asyncio.QueueFull:
                # Drop oldest block to prevent backpressure deadlock
                try:
                    self.data_queue.get_nowait()
                except asyncio.QueueEmpty:
                    pass
                try:
                    self.data_queue.put_nowait(item)
                except asyncio.QueueFull:
                    pass

        self.loop.call_soon_threadsafe(_put)

    # ─────────────────────────────────────────────────────
    # Async task: filter blocks and broadcast via WebSocket
    # ─────────────────────────────────────────────────────
    async def _filter_and_broadcast(self):
        filt = RealtimeFilter()

        while True:
            try:
                item = await self.data_queue.get()
            except Exception:
                await asyncio.sleep(0.1)
                continue

            if isinstance(item, str) and item == "RESET":
                filt.reset()
                continue

            # item is tuple (eeg_block, usb_block)
            block_eeg, block_usb = item
            filtered_eeg = filt.process(block_eeg)
            self.block_num += 1

            # Build binary message:
            #   [4-byte uint32 block_num LE] + [float32 planar EEG] + [float32 planar USB]
            header = struct.pack('<I', self.block_num)
            planar_eeg_bytes = filtered_eeg.astype(np.float32).T.tobytes()
            planar_usb_bytes = block_usb.astype(np.float32).T.tobytes()

            await manager.broadcast_bytes(header + planar_eeg_bytes + planar_usb_bytes)

    # ─────────────────────────────────────────────────────
    # Async task: broadcast system status at 1 Hz
    # ─────────────────────────────────────────────────────
    async def _status_broadcast_loop(self):
        while True:
            await asyncio.sleep(1.0)
            status = {
                "cpu": round(self.cpu, 1),
                "ram": round(self.ram, 1),
                "pkt_loss": self.pkt_loss,
                "state": self.current_state,
                "block_num": self.block_num,
            }
            if self.imp_values is not None:
                status["imp"] = [round(v, 2) for v in self.imp_values]
            await status_manager.broadcast_json(status)

    # ─────────────────────────────────────────────────────
    # Shutdown
    # ─────────────────────────────────────────────────────
    def shutdown(self):
        self.stop_event.set()
        if self.process and self.process.poll() is None:
            print("[MAIN] Terminating C process...")
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                print("[MAIN] Forcing process kill...")
                self.process.kill()
                self.process.wait()

        # Close stdout pipe to unblock reader thread
        try:
            self.process.stdout.close()
        except Exception:
            pass

        print("[MAIN] Shutdown complete.")


mon_process = MonEEEProcess()



# ═══════════════════════════════════════════════════════════
# HTTP Routes
# ═══════════════════════════════════════════════════════════

@app.get("/", response_class=HTMLResponse)
async def get_index(request: Request):
    return templates.TemplateResponse("index.html", {"request": request})


class CommandRequest(BaseModel):
    command: str


@app.post("/api/command")
async def handle_command(cmd: CommandRequest):
    if cmd.command == "START_EEG":
        if mon_process.current_state != "IDLE":
            return {"status": f"Already in {mon_process.current_state}"}
        # Reset filter and clear buffers
        mon_process.eeg_buffer.clear()
        mon_process.block_num = 0
        if mon_process.data_queue:
            await mon_process.data_queue.put("RESET")
        mon_process.send_command(CMD_START_EEG)
        # Don't set state here — let reader thread detect
        # "[CMD] Mode EEG started." to avoid race condition
        return {"status": "EEG Start sent"}

    elif cmd.command == "START_IMP":
        if mon_process.current_state != "IDLE":
            return {"status": f"Already in {mon_process.current_state}"}
        mon_process.send_command(CMD_START_IMP)
        return {"status": "Impedance Start sent"}

    elif cmd.command == "STOP":
        mon_process.send_command(CMD_STOP)
        return {"status": "Stop sent"}

    return {"status": "Unknown command"}


# ═══════════════════════════════════════════════════════════
# WebSocket endpoints
# ═══════════════════════════════════════════════════════════

@app.websocket("/ws")
async def websocket_eeg(websocket: WebSocket):
    """Binary EEG data stream."""
    await manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        manager.disconnect(websocket)


@app.websocket("/ws/status")
async def websocket_status(websocket: WebSocket):
    """JSON system status stream (1 Hz)."""
    await status_manager.connect(websocket)
    try:
        while True:
            await websocket.receive_text()
    except WebSocketDisconnect:
        status_manager.disconnect(websocket)


# ═══════════════════════════════════════════════════════════
# API: get current status (for initial page load)
# ═══════════════════════════════════════════════════════════

@app.get("/api/status")
async def get_status():
    return {
        "cpu": round(mon_process.cpu, 1),
        "ram": round(mon_process.ram, 1),
        "pkt_loss": mon_process.pkt_loss,
        "state": mon_process.current_state,
        "block_num": mon_process.block_num,
    }


if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=8000, reload=False)
