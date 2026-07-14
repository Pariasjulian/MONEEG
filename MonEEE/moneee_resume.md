# MonEEE — Project Summary

## System Overview

The MonEEE project is a 32-channel EEG data acquisition system built on a
**TM4C1294NCPDT** (ARM Cortex-M4F, 120 MHz) controlling four **ADS1299**
analog front-ends via SPI.  A host compute module (**CM5**, Raspberry Pi class)
sends commands and clocks out data through a second SPI bus (SSI0 slave).

---

## Hardware Architecture

| Block          | Detail                                                     |
|----------------|------------------------------------------------------------|
| **MCU**        | TM4C1294NCPDT @ 120 MHz (TI TivaC)                        |
| **AFE**        | 4× ADS1299 (8 ch each → 32 ch total, 24-bit Σ-Δ)         |
| **ADS SPI**    | SSI2 master, Mode 1, 4 MHz, manual CS (PD2/PN1/PN4/PJ1)   |
| **Host SPI**   | SSI0 slave,  Mode 0, CM5 clocks at ≤4 MHz                 |
| **DRDY**       | PP4 — falling-edge ISR (`ADSIntHandler`)                   |
| **Host IRQ**   | PA0 — falling-edge from CM5 (`RPIIntHandler`)              |
| **Data Ready** | PA1 — TM4C → CM5 output, pulled LOW when data is ready    |
| **Debug**      | UART2 @ 115 200 baud (PA6/PA7)                            |
| **USB CDC**    | USB0 Device — receives per-ADS event bytes (`usb_buffer`)  |
| **RESET pins** | PP3 / PN0 / PN3 / PJ0  (active-low, one per ADS)          |
| **PWDN pins**  | PQ4 / PP5 / PN2 / PN5  (active-low, one per ADS)          |
| **START pin**  | PP2 — global start, active-high                            |
| **Clock**      | ADS1 generates CLK out → ADS2/3/4 use it as external CLK  |

### SPI Topology

```
CM5 (master)            TM4C (slave SSI0)           4× ADS1299 (slaves SSI2)
  ┌────┐  PA0 falling   ┌──────────┐    DRDY PP4    ┌────────┐
  │    │───────────────►│RPIIntHdlr│◄───────────────│ADS1299 │×4
  │    │  SPI0 CLK/MISO │          │  SSI2 CLK/MOSI │        │
  │    │◄───────────────│SSI0IntHdl│───────────────►│        │
  └────┘                └──────────┘                └────────┘
```

---

## Operational Modes

Commands received via SPI slave RX FIFO inside `RPIIntHandler`:

| Cmd    | Name                | Description                                         |
|--------|---------------------|-----------------------------------------------------|
| `0x01` | **EEG**             | Continuous 250 SPS, Gain 24, normal MUX              |
| `0x02` | **STOP**            | Halt any running mode                                |
| `0x03` | **Impedance**       | One-shot lead-off sweep (6 µA, 31.2 Hz AC, Gain 1)  |
| `0x04` | **Test (shorted)**  | Internal short-circuit noise test, Gain 24           |

### Mode Details

#### 1. EEG Mode (`startEEGMode`)

* **CONFIG1:** ADS1 = `0xF6` (master, CLK out, 250 SPS) / ADS2-4 = `0xD6` (slave, CLK in)
* **CONFIG3:** ADS1 = `0xEC` (int ref + bias buffer on) / ADS2-4 = `0xE8` (int ref, bias off)
* **CHnSET:**  `0x68` → PD=0, **Gain=24**, SRB2=closed, MUX=normal input
* **BIAS:**    `BIAS_SENSN=0xFF` on ADS1 only (all INxN → bias derivation)
* **Lead-off:** All disabled

#### 2. Impedance Mode (`startImpedanceMode` + `measureAllImpedances`)

* **LOFF register:** `0x0A` → 6 µA, 31.2 Hz AC excitation, 95 % threshold
* **CHnSET:**  `0x08` → Gain=1, SRB2=closed, MUX=normal input
* Sequentially sweeps 32 channels:
  - Sets `LOFF_SENSN` bit for one channel at a time
  - Discards 10 settling samples, then collects 32 raw samples into `Electrode_Impedance_Ohms[ch][32]`
* After sweep: `calculateSendPkPk()` computes peak-to-peak per channel, packs into `eeeData[112]`, signals CM5

#### 3. Test Shorted Mode (`startTestShorted`)

* **CHnSET:**  `0x69` → PD=0, **Gain=24**, SRB2=closed, MUX=**input shorted**
* All other registers identical to EEG mode
* Continuous RDATAC stream like EEG

---

## Data Path (EEG / Test)

```
DRDY falls ──► ADSIntHandler ISR
                │
                ├── CS_LOW(ADS1) → clock 27 bytes → CS_HIGH
                ├── CS_LOW(ADS2) → clock 27 bytes → CS_HIGH
                ├── CS_LOW(ADS3) → clock 27 bytes → CS_HIGH
                └── CS_LOW(ADS4) → clock 27 bytes → CS_HIGH
                │
                ├── Inject frame markers [0x00][0x7E/7D/7C/7B]
                ├── Inject USB event bytes from usb_buffer[]
                ├── Reset pos=0, clear usb_buffer, set PI_DRDY=true
                └── Pull PA1 LOW  →  CM5 sees data ready
                                          │
                CM5 clocks SSI0 ──► SSI0IntHandler
                                    ├── PA1 HIGH (ack)
                                    └── Push eeeData[pos++] into TX FIFO
```

### `eeeData[]` Frame Layout (112 useful bytes)

| Offset  | Content              | Size   |
|---------|----------------------|--------|
| 0–1     | `[0x00][0x7E]`       | 2 B    |
| 2–3     | USB event ADS1       | 2 B    |
| 4–27    | CH0–CH7 (24-bit ×8)  | 24 B   |
| 28–29   | `[0x00][0x7D]`       | 2 B    |
| 30–31   | USB event ADS2       | 2 B    |
| 32–55   | CH8–CH15             | 24 B   |
| 56–57   | `[0x00][0x7C]`       | 2 B    |
| 58–59   | USB event ADS3       | 2 B    |
| 60–83   | CH16–CH23            | 24 B   |
| 84–85   | `[0x00][0x7B]`       | 2 B    |
| 86–87   | USB event ADS4       | 2 B    |
| 88–111  | CH24–CH31            | 24 B   |

**Total payload:** 4 × (2 marker + 2 USB + 24 data) = **112 bytes/sample**

`eeeData` is declared as `uint8_t eeeData[140]` (28 bytes of headroom).

---

## File Structure

```
MonEEE/
├── main.c                        Application entry, ISRs, super-loop
├── tm4c1294ncpdt_startup_ccs.c   Vector table (maps ISRs)
├── tm4c1294ncpdt.cmd             Linker command file
├── Drivers/
│   ├── Board.h / Board.c         GPIO, SPI, UART, USB, SysTick init
│   └── usb_structs.h / .c       USB CDC descriptors and buffers
├── Modules/
│   ├── ADS1299.h                 Types, enums, register-command API
│   ├── ADS1299.c                 ADS logic, ISRs, mode functions
│   ├── Defines_ADS1299.h         Register addresses and bit constants
│   └── delay.h / delay.c         Busy-wait delays + constrain()
├── utils/                        (empty — headers resolved from TivaWare)
└── moneee_resume.md              This document
```

---

## Global Variable Cross-Reference

| Variable                      | Defined in   | Used in              | volatile? | Notes                                    |
|-------------------------------|--------------|----------------------|-----------|------------------------------------------|
| `eeeData[140]`                | main.c       | ADS1299.c (extern)   | **NO ⚠️**  | Written in ISR, read in main context     |
| `usb_buffer[10]`              | main.c       | Board.c, ADS1299.c   | **NO ⚠️**  | Written in USB RX ISR, read in DRDY ISR  |
| `usbCont`                     | main.c       | Board.c, ADS1299.c   | **NO ⚠️**  | Written in USB RX ISR, read in DRDY ISR  |
| `statusRPI`                   | main.c       | main.c only          | **NO ⚠️**  | Written/read in RPIIntHandler ISR + considered in main loop context |
| `flagIMP`                     | main.c       | main.c, ADS1299.c    | **NO ⚠️**  | Set in ISR, polled in main loop          |
| `Electrode_Impedance_Ohms[32][32]` | main.c  | ADS1299.c            | no        | OK — only used from main-loop context    |
| `g_cm5Command`                | main.c       | main.c only          | ✅ yes     | Correct                                  |
| `g_ui32Seconds`               | main.c       | main.c only          | **NO ⚠️**  | Incremented in SysTick ISR               |
| `PI_DRDY`                     | ADS1299.c    | ADS1299.c            | **NO ⚠️**  | Set in ISR, polled in while-loops        |
| `pos`                         | ADS1299.c    | ADS1299.c            | **NO ⚠️**  | Written in ADSIntHandler, read in SSI0IntHandler |
| `g_acqMode`                   | ADS1299.h    | — (never defined)    | —         | **Declared extern but never instantiated ⚠️** |
| `regData[24]`                 | ADS1299.h    | ADS1299.c            | no        | OK — register mirror, non-ISR           |
| `curSampleRate`               | ADS1299.c    | ADS1299.c            | no        | OK — only read in initADS               |
| `numChannels`                 | ADS1299.c    | ADS1299.c            | no        | OK — only read in initADS               |

---

## Issues Found

### Critical — Missing `volatile` Qualifiers

Multiple variables are shared between ISR context and main-loop (or between two different ISRs) but lack `volatile`. Without `volatile`, the compiler may cache the value in a register and the main loop may never see updates:

| Variable         | Shared between                        |
|------------------|---------------------------------------|
| `eeeData[140]`   | ADSIntHandler ↔ SSI0IntHandler        |
| `usb_buffer[10]` | RxHandler (USB ISR) ↔ ADSIntHandler   |
| `usbCont`        | RxHandler (USB ISR) ↔ ADSIntHandler   |
| `flagIMP`        | RPIIntHandler ↔ main loop             |
| `statusRPI`      | RPIIntHandler (ISR-only, but may be optimized out) |
| `g_ui32Seconds`  | SysTickIntHandler ↔ main startup loop |
| `PI_DRDY`        | ADSIntHandler ↔ measureAllImpedances (main context) |
| `pos`            | ADSIntHandler ↔ SSI0IntHandler        |

### Critical — `g_acqMode` Declared but Never Defined

`ADS1299.h` declares `extern volatile AcqMode_t g_acqMode;` but no `.c` file in the project contains its definition. The linker will emit an unresolved-symbol error if anything references it. Currently nothing does, so it is **dead code** — the `extern` and the `AcqMode_t` enum should either be utilized or removed.

### Moderate — `ACQ_MODE_IDLE` Value vs. Comment Mismatch

The header comment says `ACQ_MODE_IDLE = 0x00` but the actual enum sets it to `0x05`.
The guards in `RPIIntHandler` compare `g_cm5Command != ACQ_MODE_IDLE` — since `g_cm5Command` can only be 0x01–0x04 from the CM5, the comparison `(0x01 != 0x05)` is **always true**, making the guard checks into no-ops.

### Moderate — `RPIIntHandler` Guard Logic is Tautological

In every `case` branch, the code checks `if (g_cm5Command != ACQ_MODE_IDLE)`. But `g_cm5Command` was *just assigned* the value that entered that `case`, so:
- `case CM5_CMD_START_EEG:` → `g_cm5Command == 0x01`, and `0x01 != 0x05` is always true
- `case CM5_CMD_STOP:` → `g_cm5Command == 0x02`, and `0x02 != 0x05` is always true

The intended logic was likely to check a **separate state variable** (like `statusRPI`) to decide whether a stop is needed, not to re-check the command byte that just entered the case.

### Minor — Orphaned `extern` in ADS1299.c

`extern uint32_t ui32RxData;` in ADS1299.c references a **local variable** inside `RPIIntHandler()`. This is not a valid extern link — `ui32RxData` is a stack variable, not a global. The extern is never actually used in ADS1299.c, so it compiles, but it is dead code.

### Minor — `initI2C()` Defined but Never Called

`Board.c` defines `initI2C()` but no code in the project calls it, and it is not declared in `Board.h`. It can be removed to save flash.

### Minor — `regData[24]` Declared in Header (Not Source)

`uint8_t regData[24]` is declared as a **definition** (not `extern`) in `ADS1299.h`. Every `.c` file that includes the header would get its own copy, leading to duplicate symbols. It works only because the linker merges them (common symbols in C), but the correct pattern is `extern uint8_t regData[24];` in the header and the definition in `ADS1299.c`.

### Minor — `eeeData[140]` Oversized

Only 112 bytes are ever written. The buffer could be reduced to `eeeData[112]` to save 28 bytes of RAM. The extra headroom was likely inherited from an older protocol but is no longer needed.

---

## RAM Usage Estimate

| Variable                       | Size (bytes) |
|--------------------------------|--------------|
| `eeeData[140]`                 | 140          |
| `Electrode_Impedance_Ohms[32][32]` | 4 096    |
| `usb_buffer[10]`               | 10           |
| `regData[24]`                  | 24           |
| USB TX/RX buffers (256 each)   | 512          |
| Stack + other locals           | ~2 048       |
| **Total estimate**             | **~6 830**   |

The TM4C1294NCPDT has **256 KB SRAM**, so usage is well within budget.
`Electrode_Impedance_Ohms` is the largest consumer at 4 KB; this is acceptable since impedance mode needs 32×32 samples.

---

## Recommendations

1. **Add `volatile` to all ISR-shared variables** — `eeeData`, `usb_buffer`, `usbCont`, `flagIMP`, `statusRPI`, `g_ui32Seconds`, `PI_DRDY`, `pos`. This is the most impactful correctness fix.
2. **Remove dead `extern volatile AcqMode_t g_acqMode`** from `ADS1299.h` and the unused `AcqMode_t` enum (or instantiate it if it will be used in the future).
3. **Fix `RPIIntHandler` guard logic** — replace `g_cm5Command != ACQ_MODE_IDLE` with a proper state check (e.g., just use `statusRPI`).
4. **Remove `extern uint32_t ui32RxData`** from ADS1299.c.
5. **Remove `initI2C()`** from Board.c (dead code).
6. **Move `regData[24]` definition** from header to ADS1299.c; use `extern` in header.
7. **Shrink `eeeData`** from 140 to 112 if no future expansion is planned.
