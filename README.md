<p align="center">
  <img src="https://img.shields.io/badge/Channels-32-00d4ff?style=for-the-badge" alt="32 Channels"/>
  <img src="https://img.shields.io/badge/Resolution-24--bit-7c3aed?style=for-the-badge" alt="24-bit"/>
  <img src="https://img.shields.io/badge/Sample%20Rate-250%20SPS-10b981?style=for-the-badge" alt="250 SPS"/>
  <img src="https://img.shields.io/badge/MCU-TM4C1294-f59e0b?style=for-the-badge" alt="TM4C1294"/>
</p>

# MonEEG — 32-Channel EEG Acquisition System

> **A real-time, research-grade electroencephalography platform** built on a TM4C1294NCPDT (ARM Cortex-M4F, 120 MHz) controlling four ADS1299 analog front-ends, with a CM5 (Raspberry Pi–class) compute module for streaming, filtering, and web-based visualization.

📖 **Interactive Documentation:**
* **[Online (GitHub Pages) →](https://pariasjulian.github.io/MONEEG/docs/index.html)**
* **[Online (HTML Preview) →](https://htmlpreview.github.io/?https://github.com/Pariasjulian/MONEEG/blob/main/docs/index.html)**
* **[Local Server (http://localhost:8080/index.html) →](http://localhost:8080/index.html)**


---

## System Overview

MonEEG captures 32 channels of 24-bit EEG data at 250 SPS, applying real-time IIR filtering (4th-order Butterworth bandpass + 60 Hz notch) and streaming filtered data over WebSocket to a browser-based visualization dashboard.

```
┌──────────────────┐   SPI (SSI2, 4 MHz)   ┌──────────────────┐
│   4× ADS1299     │ ◄──────────────────── │    TM4C1294      │
│  (8 ch each)     │ ──────────────────── ► │  ARM Cortex-M4F  │
│  24-bit Σ-Δ ADC  │        DRDY (PP4)      │  120 MHz         │
└──────────────────┘                        └────────┬─────────┘
                                                     │ SPI (SSI0, slave)
                                                     │ PA0 (CMD), PA1 (DRDY)
                                            ┌────────▼─────────┐
                                            │   CM5 / Raspi    │
                                            │  SPI Master      │ ─── .bin file
                                            │  C daemon        │
                                            └────────┬─────────┘
                                                     │ stdin/stdout pipe
                                            ┌────────▼─────────┐
                                            │  Python/FastAPI  │
                                            │  IIR Filter      │ ─── WebSocket
                                            │  WebSocket srv   │
                                            └────────┬─────────┘
                                                     │ ws://
                                            ┌────────▼─────────┐
                                            │  Browser UI      │
                                            │  Real-time plots │
                                            └──────────────────┘
```

---

## Hardware Architecture

| Block          | Detail                                                     |
|----------------|-------------------------------------------------------------|
| **MCU**        | TM4C1294NCPDT @ 120 MHz (TI TivaC, ARM Cortex-M4F)        |
| **AFE**        | 4× ADS1299 (8 ch each → 32 ch total, 24-bit Σ-Δ)          |
| **ADS SPI**    | SSI2 master, Mode 1, 4 MHz, manual CS (PD2/PN1/PN4/PJ1)   |
| **Host SPI**   | SSI0 slave, Mode 0, CM5 clocks at ≤4 MHz                   |
| **DRDY**       | PP4 — falling-edge ISR (`ADSIntHandler`)                    |
| **Host IRQ**   | PA0 — falling-edge from CM5 (`RPIIntHandler`)               |
| **Data Ready** | PA1 — TM4C → CM5 output, pulled LOW when data is ready     |
| **Debug**      | UART2 @ 115200 baud (PA6/PA7)                               |
| **USB CDC**    | USB0 Device — receives per-ADS event bytes                   |
| **Clock**      | ADS1 generates CLK out → ADS2/3/4 use external CLK          |

---

## Operational Modes

| Cmd    | Mode              | Description                                           |
|--------|-------------------|-------------------------------------------------------|
| `0x01` | **EEG**           | Continuous 250 SPS, Gain 24, normal MUX               |
| `0x02` | **STOP**          | Halt any running acquisition mode                     |
| `0x03` | **Impedance**     | One-shot lead-off sweep (6 µA, 31.2 Hz AC, Gain 1)   |
| `0x04` | **Test (shorted)**| Internal short-circuit noise test, Gain 24            |

---

## Data Frame Format (112 bytes per sample)

Each DRDY interrupt produces one frame of 4 event blocks:

| Offset  | Content               | Size   |
|---------|-----------------------|--------|
| 0–1     | `[0x00][0x7E]`        | 2 B    |
| 2–3     | USB event ADS1        | 2 B    |
| 4–27    | CH0–CH7 (24-bit × 8)  | 24 B   |
| 28–29   | `[0x00][0x7D]`        | 2 B    |
| 30–31   | USB event ADS2        | 2 B    |
| 32–55   | CH8–CH15              | 24 B   |
| 56–57   | `[0x00][0x7C]`        | 2 B    |
| 58–59   | USB event ADS3        | 2 B    |
| 60–83   | CH16–CH23             | 24 B   |
| 84–85   | `[0x00][0x7B]`        | 2 B    |
| 86–87   | USB event ADS4        | 2 B    |
| 88–111  | CH24–CH31             | 24 B   |

**Total payload:** 4 × (2 marker + 2 USB + 24 data) = **112 bytes/sample**

---

## EEG Acquisition Pipeline (BPMN)

The acquisition process spans three architectural layers:

```
Session Start
      │
      ▼
┌─────────────────────────────────────────────┐
│  Physical & Analog Layer                    │
│  ├── Execute AC Impedance Measurement       │
│  ├── Mitigate Hardware SNR (Active DRL)     │
│  └── Route Data via Independent SPI Buses   │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│  Deterministic Hardware Layer               │
│  ├── Trigger Hardware-Level ISR             │
│  └── Inject Event Marker (Temporal Sync)    │
└─────────────────────┬───────────────────────┘
                      │
┌─────────────────────▼───────────────────────┐
│  Compute & Processing Layer                 │
│  ├── Execute DSP (IIR Filtering)            │
│  └── Format to XDF & Stream Data            │
└─────────────────────┬───────────────────────┘
                      │
                      ▼
               Session End
```

---

## Repository Structure

```
MONEEG/
├── MonEEE/                         TM4C1294 Firmware (CCS Project)
│   ├── main.c                      Application entry, ISRs, super-loop
│   ├── tm4c1294ncpdt_startup_ccs.c Vector table (maps ISRs)
│   ├── tm4c1294ncpdt.cmd           Linker command file
│   ├── Drivers/
│   │   ├── Board.h / Board.c       GPIO, SPI, UART, USB, SysTick init
│   │   └── usb_structs.h / .c      USB CDC descriptors and buffers
│   ├── Modules/
│   │   ├── ADS1299.h / .c          ADS1299 driver, ISRs, mode functions
│   │   ├── Defines_ADS1299.h       Register addresses and bit constants
│   │   └── delay.h / .c            Busy-wait delays
│   └── moneee_resume.md            Detailed firmware technical reference
│
├── Raspi/                          CM5 Host Software
│   ├── main.c                      SPI data acquisition daemon (C, pthreads)
│   ├── main.py                     FastAPI real-time EEG server (Python)
│   ├── Drivers/
│   │   ├── bacn_gpio.h / .c        GPIO control (DRDY, START pins)
│   │   └── sys_monitor.h / .c      CPU/RAM usage metrics
│   ├── templates/
│   │   └── index.html              Browser-based EEG visualization dashboard
│   └── static/                     Fonts and vendor assets
│
├── Moneeg.py                       BPMN diagram generator (Python)
├── eeg_acquisition_framework.bpmn  Acquisition pipeline definition (BPMN 2.0)
├── docs/                           Interactive documentation website
│   ├── index.html
│   ├── styles.css
│   └── script.js
└── README.md                       This file
```

---

## Software Stack

| Layer                | Technology                    | Role                                   |
|----------------------|-------------------------------|----------------------------------------|
| **Firmware**         | C (TivaWare, CCS)            | ADS1299 control, ISR-driven sampling   |
| **Host Daemon**      | C (Linux, spidev, pthreads)  | SPI transfer, frame parsing, .bin file |
| **Server**           | Python (FastAPI, SciPy)       | IIR filtering, WebSocket streaming     |
| **Frontend**         | HTML/JS (WebSocket)           | Real-time EEG waveform visualization   |

---

## Quick Start

### Firmware (TM4C1294)
1. Open `MonEEE/` as a project in **Code Composer Studio** (TI CCS)
2. Build and flash to the TM4C1294NCPDT

### Host Software (CM5 / Raspberry Pi)
```bash
# Compile the C daemon
cd Raspi
gcc -o moneee main.c Drivers/bacn_gpio.c Drivers/sys_monitor.c -lpthread

# Install Python dependencies
pip install fastapi uvicorn numpy scipy jinja2

# Run the server
python main.py
```

### Access the Dashboard
Open a browser and navigate to `http://<CM5-IP>:8000`

---

## License

*License pending — please add your preferred license.*
