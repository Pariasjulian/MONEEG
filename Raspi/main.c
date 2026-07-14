/**
 * @file    main.c
 * @brief   CM5 SPI Data Acquisition — ADS1299 (EEG + Impedance + Test)
 * @version 5.2 — triple-mode unified: EEG + Impedance + Test (all 135B SPI, 112B payload, .bin)
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  IPC PROTOCOL  (Python → CM5, binary via stdin pipe)               │
 * │                                                                     │
 * │  0x01 = Start EEG acquisition                                       │
 * │  0x02 = Stop (any mode)                                             │
 * │  0x03 = Start impedance measurement                                 │
 * │  0x04 = Start internal test (ADS1299 test signal)                   │
 * │                                                                     │
 * │  Before Start_ADQ(), the command is forwarded via SPI to the ARM    │
 * │  so it configures the corresponding mode.                           │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  SPI PROTOCOL — EEG  (ARM → CM5, 135 bytes per transfer)          │
 * │                                                                     │
 * │  The ARM does not guarantee the frame starts at byte 0.            │
 * │  There may be up to 23 bytes of variable offset at the beginning.  │
 * │                                                                     │
 * │  Structure of each event (28 bytes = 32-bit HDR + 24 bytes CH):   │
 * │                                                                     │
 * │  ┌────────┬────────┬──────────┬──────────┬──────────────────────┐  │
 * │  │ 0x00   │ 0x7E   │ USBEVT_H │ USBEVT_L │  CH0..CH7 (24 B)  │  │
 * │  │ 0x00   │ 0x7D   │ USBEVT_H │ USBEVT_L │  CH8..CH15         │  │
 * │  │ 0x00   │ 0x7C   │ USBEVT_H │ USBEVT_L │  CH16..CH23        │  │
 * │  │ 0x00   │ 0x7B   │ USBEVT_H │ USBEVT_L │  CH24..CH31        │  │
 * │  └────────┴────────┴──────────┴──────────┴──────────────────────┘  │
 * │  └── MARK_SIZE=2 ──┘└── USB_EVT_SIZE=2 ──┘└── CH_BYTES=24 ────┘  │
 * │  └──────────── EVENT_HDR=4 (32 bits) ────┘                         │
 * │  └────────────────── EVENT_BLOCK=28 ───────────────────────────┘  │
 * │                                                                     │
 * │  Clean payload → .bin: 4 × 28 = 112 bytes (no offset)             │
 * │  CSV payload:  32 CH hex + 4 USB events hex + CPU + RAM           │
 * └─────────────────────────────────────────────────────────────────────┘
 *
 * ┌─────────────────────────────────────────────────────────────────────┐
 * │  SPI PROTOCOL — IMPEDANCE  (ARM → CM5, 135-byte buffer)           │
 * │                                                                     │
 * │  Same format as EEG: 4 events × 28 bytes = 112 bytes payload      │
 * │  Uses parse_frame() and writes .bin just like EEG/TEST.            │
 * │  CSV output controlled by #define BIN_ONLY_IMP (compile-time).     │
 * └─────────────────────────────────────────────────────────────────────┘
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <linux/spi/spidev.h>
#include <pthread.h>
#include <time.h>

#include "Drivers/bacn_gpio.h"
#include "Drivers/sys_monitor.h"

/* ─────────────────────────────── SPI ─────────────────────────────── */
#define SPI_DEVICE      "/dev/spidev0.0"
#define SPI_SPEED       2000000U    /* 2 MHz                */
#define SPI_BITS        8           /* bits per word         */

/* ──────────────────── IPC Commands (binary) ──────────────────────── */
#define CMD_START_EEG   0x01        /* Python → CM5: start EEG         */
#define CMD_STOP        0x02        /* Python → CM5: stop              */
#define CMD_START_IMP   0x03        /* Python → CM5: start impedance   */
#define CMD_START_TEST  0x04        /* Python → CM5: start test        */

/* ──────────────────── Operating Mode ─────────────────────────────── */
typedef enum { MODE_IDLE, MODE_EEG, MODE_IMP, MODE_TEST } acq_mode_t;

/* ──────────────── EEG: Protocol (135 bytes SPI) ──────────────────── */
#define FRAME_BYTES     135         /* total SPI buffer                                */

#define NUM_EVENTS      4           /* events per frame                                */
#define CH_PER_EVENT    8           /* channels per event                              */
#define TOTAL_CH        (NUM_EVENTS * CH_PER_EVENT)  /* 32 channels                   */

#define MARK_SIZE       2           /* [0x00][0x7x] — start frame marker               */
#define USB_EVT_SIZE    2           /* [USB_H][USB_L] — USB event from ARM             */
#define EVENT_HDR       (MARK_SIZE + USB_EVT_SIZE)   /* 4 bytes = 32-bit header        */
#define CH_BYTES        (CH_PER_EVENT * 3)           /* 24 bytes of channel data/event  */
#define EVENT_BLOCK     (EVENT_HDR + CH_BYTES)       /* 28 bytes/event                  */
#define PAYLOAD_BYTES   (NUM_EVENTS * EVENT_BLOCK)   /* 112 bytes clean frame           */
/* 4 × 28 = 112 useful;  135 - 112 = 23 bytes max variable offset                     */

/* Offsets within an event block */
#define OFF_MARK        0           /* byte 0-1: start frame marker                    */
#define OFF_USB         MARK_SIZE   /* byte 2-3: USB event                             */
#define OFF_CH          EVENT_HDR   /* byte 4-27: channel data                         */

/* EEG start markers (high byte = 0x00, low byte = mark) */
static const uint8_t EVENT_MARKS[NUM_EVENTS] = {0x7E, 0x7D, 0x7C, 0x7B};

/* ──────────── IMP: Compile-time configuration ────────────────────── */
#define BIN_ONLY_IMP      0         /* 1 = IMP records .bin, 0 = CSV only (no .bin)    */
#define IMP_CONTINUOUS    0         /* 1 = impedance runs continuously like EEG         */
                                    /* 0 = single sweep then auto-stop (one-shot)       */

/* ──────────── Binary file: flush interval ────────────────────────── */
#define BIN_FLUSH_INTERVAL 250      /* flush .bin every N samples (~1 s at 250 SPS)    */

/* ──────────────────────────── Globals ────────────────────────────── */
static uint8_t          spi_mode = SPI_MODE_0;
static int              spi_fd   = -1;
static FILE            *bin_file = NULL;

/* SPI buffers */
static uint8_t  data_tx[FRAME_BYTES];       /* TX dummy (CM5 sends no data)            */
static uint8_t  data_rx[FRAME_BYTES];       /* RX: raw frame, 135 bytes from ARM       */
static uint8_t  payload[PAYLOAD_BYTES];     /* clean frame: 4 × 28 = 112 bytes         */

/* All modes use data_rx[FRAME_BYTES] and payload[PAYLOAD_BYTES] */

static volatile sig_atomic_t g_shutdown  = 0;  /* set by signal handler only            */
static volatile acq_mode_t   current_mode = MODE_IDLE;
volatile sig_atomic_t    running      = 0;      /* flag read by AcqThread                */
volatile uint32_t       lost_packets = 0;      /* lost frame counter                    */

volatile double current_cpu = 0.0;
volatile double current_ram = 0.0;

static pthread_t        thread_acq;
static pthread_t        thread_sys;
static pthread_mutex_t  file_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ──────────────────────────── Prototypes ─────────────────────────── */
static int   spi_init(const char *device, uint8_t mode, uint8_t bits, uint32_t speed);
static int   spi_transfer(int fd, uint8_t *tx, uint8_t *rx, uint32_t len);
static void  send_spi_command(uint8_t cmd);
static int   parse_frame(void);             /* find frame → fill payload[]             */
static void  write_payload_to_bin(int flush);/* write payload[112] to .bin              */
static void  create_bin_file(void);
static void  check_input(void);
static void  start_acquisition(acq_mode_t mode);
static void  stop_acquisition(void);

/* ══════════════════════════════════════════════════════════════════════
 * THREAD: System monitoring — 1 Hz
 * ══════════════════════════════════════════════════════════════════════ */
void *SysMonitorThread(void *arg)
{
    (void)arg;
    sys_metrics_t metrics;
    while (!g_shutdown) {
        if (get_sys_metrics(&metrics) == 0) {
            current_cpu = metrics.cpu_usage;
            current_ram = metrics.ram_usage;
        }
        sleep(1);
    }
    pthread_exit(NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 * THREAD: Unified acquisition (EEG / Impedance / Test)
 *
 * All modes use the same flow:
 *   135 bytes SPI → parse_frame() → 112 bytes payload → .bin
 *   CSV output via stdout (IMP: controlled by BIN_ONLY_IMP)
 * ══════════════════════════════════════════════════════════════════════ */
void *AcqThread(void *arg)
{
    (void)arg;
    const acq_mode_t mode = current_mode;   /* capture mode at start */
    const char *mode_str = (mode == MODE_EEG) ? "EEG" :
                           (mode == MODE_TEST) ? "TEST" : "IMP";
    uint32_t bin_count = 0;                 /* sample counter for flush interval */

    printf("[ACQ] Starting mode %s...\n", mode_str);

    while (running) {
        if (status_DRDY() == 0) {

            /* ── All modes: transfer 135 bytes SPI ── */
            if (spi_transfer(spi_fd, data_tx, data_rx, FRAME_BYTES) < 0) {
                perror("[ERROR] SPI transfer");
                continue;
            }

            if (parse_frame() >= 0) {
                /* .bin: EEG/TEST always, IMP only if BIN_ONLY_IMP */
                if (mode != MODE_IMP || BIN_ONLY_IMP) {
                    bin_count++;
                    int do_flush = (bin_count % BIN_FLUSH_INTERVAL) == 0;
                    write_payload_to_bin(do_flush);
                }

                /* CSV stdout: all modes */
                /* ── Print 32 channels (6 hex chars each) ── */
                for (int e = 0; e < NUM_EVENTS; e++) {
                    const uint8_t *evb = &payload[e * EVENT_BLOCK + OFF_CH];
                    for (int ch = 0; ch < CH_PER_EVENT; ch++) {
                        int sep = (e * CH_PER_EVENT + ch) < (TOTAL_CH - 1);
                        printf("%02X%02X%02X%s",
                               evb[ch*3], evb[ch*3+1], evb[ch*3+2],
                               sep ? "," : "");
                    }
                }

                /* ── 4 USB events (4 hex chars each) ── */
                for (int e = 0; e < NUM_EVENTS; e++) {
                    printf(",%02X%02X",
                           payload[e * EVENT_BLOCK + OFF_USB],
                           payload[e * EVENT_BLOCK + OFF_USB + 1]);
                }

                printf(",%.2f,%.2f,%u\n", current_cpu, current_ram, lost_packets);

                /* ── Impedance one-shot: auto-stop after first valid frame ── */
                if (mode == MODE_IMP && !IMP_CONTINUOUS) {
                    printf("[ACQ] Impedance sweep complete (one-shot mode).\n");
                    running = 0;
                    break;
                }
            } else {
                lost_packets++;
            }
        }
        usleep(200);   /* 200 µs — CPU / latency balance for 250–500 Hz */
    }

    printf("[ACQ] Thread finished (mode %s).\n", mode_str);
    fflush(stdout);
    pthread_exit(NULL);
}

/* ══════════════════════════════════════════════════════════════════════
 * SIGNAL: SIGINT / SIGTERM
 *
 * Only sets async-signal-safe flags.  All cleanup is performed in
 * main() after the event loop exits.
 * ══════════════════════════════════════════════════════════════════════ */
void handle_sigint(int sig)
{
    (void)sig;
    g_shutdown = 1;
    running = 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════════════ */
int main(void)
{
    /* Unbuffer stdout so Python's readline doesn't block on buffered prints */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Use sigaction for reliable signal handling (no SA_RESTART) */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    if (gpio_init() < 0) {
        fprintf(stderr, "[ERROR] GPIO init\n");
        return 1;
    }

    spi_fd = spi_init(SPI_DEVICE, spi_mode, SPI_BITS, SPI_SPEED);
    if (spi_fd < 0) {
        gpio_cleanup();
        return 1;
    }

    Start_INI();

    /* Non-blocking stdin — set once for the Python pipe */
    int fl = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, fl | O_NONBLOCK);

    if (pthread_create(&thread_sys, NULL, SysMonitorThread, NULL) != 0) {
        fprintf(stderr, "[ERROR] Could not create SysMonitorThread\n");
        close(spi_fd); gpio_cleanup();
        return 1;
    }
    pthread_detach(thread_sys);

    printf("[MAIN] Ready. Commands: 0x01=EEG, 0x02=STOP, 0x03=IMP, 0x04=TEST\n");
    while (!g_shutdown) {
        check_input();

        /* Detect self-terminated acquisition (e.g., impedance one-shot) */
        if (current_mode != MODE_IDLE && !running) {
            stop_acquisition();
            printf("[CMD] Acquisition auto-stopped (one-shot mode).\n");
        }

        usleep(10000);   /* 10 ms — command polling interval */
    }

    /* ── Clean shutdown (signal received) ── */
    if (current_mode != MODE_IDLE) {
        running = 0;
        stop_acquisition();
    }

    pthread_mutex_lock(&file_mutex);
    if (bin_file) { fclose(bin_file); bin_file = NULL; }
    pthread_mutex_unlock(&file_mutex);

    if (spi_fd >= 0) close(spi_fd);
    gpio_cleanup();
    printf("\n[MAIN] Clean exit.\n");
    return 0;
}

/* ══════════════════════════════════════════════════════════════════════
 * FRAME PARSER
 *
 * Searches for the first event marker [0x00][0x7E] in data_rx[],
 * verifies that the remaining 3 markers are at EVENT_BLOCK intervals,
 * and copies all 4 complete blocks (32-bit HDR + channels) into
 * the payload[112] buffer.
 *
 * payload[] layout:
 *   payload[ 0.. 27] = Event 0: [0x00][0x7E][USB_H][USB_L] + CH0..CH7
 *   payload[28.. 55] = Event 1: [0x00][0x7D][USB_H][USB_L] + CH8..CH15
 *   payload[56.. 83] = Event 2: [0x00][0x7C][USB_H][USB_L] + CH16..CH23
 *   payload[84..111] = Event 3: [0x00][0x7B][USB_H][USB_L] + CH24..CH31
 *
 * @return offset of the first marker found in data_rx, or -1.
 * ══════════════════════════════════════════════════════════════════════ */
static int parse_frame(void)
{
    const int search_limit = FRAME_BYTES - PAYLOAD_BYTES;

    for (int i = 0; i <= search_limit; i++) {
        /* Anchor: look for [0x00][0x7E] */
        if (data_rx[i] != 0x00 || data_rx[i + 1] != EVENT_MARKS[0]) continue;

        /* Verify the next 3 markers at their exact positions */
        int found = 1;
        for (int e = 1; e < NUM_EVENTS; e++) {
            int mp = i + e * EVENT_BLOCK;
            if (data_rx[mp] != 0x00 || data_rx[mp + 1] != EVENT_MARKS[e]) {
                found = 0;
                break;
            }
        }
        if (!found) continue;

        /* Valid frame → copy all 4 complete blocks (32-bit HDR + CH) */
        memcpy(payload, &data_rx[i], PAYLOAD_BYTES);
        return i;
    }
    return -1;
}


/* ══════════════════════════════════════════════════════════════════════
 * SPI: Send command to ARM
 *
 * Sends a 1-byte command (0x01/0x02/0x03/0x04) via SPI to the ARM
 * before generating the GPIO pulse with Start_ADQ().
 * ══════════════════════════════════════════════════════════════════════ */
static void send_spi_command(uint8_t cmd)
{
    uint8_t tx = cmd, rx = 0;
    if (spi_transfer(spi_fd, &tx, &rx, 1) < 0)
        perror("[ERROR] SPI command");
    else
        printf("[SPI] Command 0x%02X sent to ARM\n", cmd);
}

/* ══════════════════════════════════════════════════════════════════════
 * CONTROL: Start acquisition (EEG, Impedance, or Test)
 * ══════════════════════════════════════════════════════════════════════ */
static void start_acquisition(acq_mode_t mode)
{
    const char *mode_str = (mode == MODE_EEG)  ? "EEG"  :
                           (mode == MODE_TEST) ? "TEST" : "IMP";
    lost_packets = 0;
    current_mode = mode;
    running = 1;

    /* Create .bin: EEG/TEST always, IMP only if BIN_ONLY_IMP */
    if (mode != MODE_IMP || BIN_ONLY_IMP)
        create_bin_file();

    if (pthread_create(&thread_acq, NULL, AcqThread, NULL) != 0) {
        fprintf(stderr, "[ERROR] Could not create AcqThread\n");
        running = 0;
        current_mode = MODE_IDLE;
        return;
    }

    /* Send command via SPI to ARM, then GPIO pulse */
    uint8_t spi_cmd = (mode == MODE_EEG)  ? CMD_START_EEG  :
                      (mode == MODE_TEST) ? CMD_START_TEST : CMD_START_IMP;
    send_spi_command(spi_cmd);
    Start_ADQ();   /* falling-edge pulse → ARM starts */

    printf("[CMD] Mode %s started.\n", mode_str);
}

/* ══════════════════════════════════════════════════════════════════════
 * CONTROL: Stop acquisition (any mode)
 *
 * Sequence: stop the acquisition thread first, then notify the ARM.
 * This prevents the thread from issuing SPI transfers after the ARM
 * has halted DRDY, which would produce stale reads and false
 * lost_packets increments.
 * ══════════════════════════════════════════════════════════════════════ */
static void stop_acquisition(void)
{
    /* 1. Stop the acquisition thread */
    running = 0;
    pthread_join(thread_acq, NULL);

    /* 2. Notify the ARM to halt (SPI command + GPIO pulse) */
    send_spi_command(CMD_STOP);
    Start_ADQ();   /* falling-edge pulse → ARM stops */

    /* 3. Flush and close .bin file */
    pthread_mutex_lock(&file_mutex);
    if (bin_file) { fflush(bin_file); fclose(bin_file); bin_file = NULL; }
    pthread_mutex_unlock(&file_mutex);

    current_mode = MODE_IDLE;
    printf("[CMD] Acquisition stopped.\n");
}

/* ══════════════════════════════════════════════════════════════════════
 * COMMAND HANDLER (binary pipe from Python)
 *
 * Reads 1 byte from stdin (non-blocking):
 *   0x01 → Start EEG
 *   0x02 → Stop
 *   0x03 → Start Impedance
 *   0x04 → Start Test
 * ══════════════════════════════════════════════════════════════════════ */
static void check_input(void)
{
    uint8_t cmd;
    if (read(STDIN_FILENO, &cmd, 1) <= 0) return;

    switch (cmd) {
    case CMD_START_EEG:
        if (current_mode != MODE_IDLE) {
            fprintf(stderr, "[CMD] Acquisition already in progress. Send 0x02 first.\n");
            break;
        }
        printf("[CMD] Received: Start EEG (0x01)\n");
        start_acquisition(MODE_EEG);
        break;

    case CMD_START_IMP:
        if (current_mode != MODE_IDLE) {
            fprintf(stderr, "[CMD] Acquisition already in progress. Send 0x02 first.\n");
            break;
        }
        printf("[CMD] Received: Start Impedance (0x03)\n");
        start_acquisition(MODE_IMP);
        break;

    case CMD_START_TEST:
        if (current_mode != MODE_IDLE) {
            fprintf(stderr, "[CMD] Acquisition already in progress. Send 0x02 first.\n");
            break;
        }
        printf("[CMD] Received: Start Test (0x04)\n");
        start_acquisition(MODE_TEST);
        break;

    case CMD_STOP:
        if (current_mode == MODE_IDLE) {
            fprintf(stderr, "[CMD] No acquisition in progress.\n");
            break;
        }
        printf("[CMD] Received: Stop (0x02)\n");
        stop_acquisition();
        break;

    default:
        fprintf(stderr, "[CMD] Unknown command: 0x%02X\n", cmd);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * BINARY FILE — Write (all modes)
 *
 * Writes the full payload[112]:
 *   4 × [0x00][0x7x][USB_H][USB_L] + 4 × 24 bytes channels
 *   = 4 × 28 = 112 bytes per sample
 *
 * Flushing is controlled by the caller via the 'flush' parameter
 * to reduce I/O overhead (see BIN_FLUSH_INTERVAL).
 * ══════════════════════════════════════════════════════════════════════ */
static void write_payload_to_bin(int flush)
{
    pthread_mutex_lock(&file_mutex);
    if (bin_file != NULL) {
        fwrite(payload, 1U, PAYLOAD_BYTES, bin_file);
        if (flush)
            fflush(bin_file);
    }
    pthread_mutex_unlock(&file_mutex);
}

/* ══════════════════════════════════════════════════════════════════════
 * BINARY FILE — Creation (all modes)
 * ══════════════════════════════════════════════════════════════════════ */
static void create_bin_file(void)
{
    time_t t = time(NULL);
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char filename[64];

    const char *prefix = (current_mode == MODE_TEST) ? "moneee_test" :
                         (current_mode == MODE_IMP)  ? "moneee_imp"  : "moneee_eeg";

    snprintf(filename, sizeof(filename),
             "%s_%04d-%02d-%02d_%02d-%02d-%02d.bin",
             prefix,
             tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);

    pthread_mutex_lock(&file_mutex);
    bin_file = fopen(filename, "wb");
    pthread_mutex_unlock(&file_mutex);

    if (!bin_file) {
        perror("[ERROR] Create binary file");
        return;
    }
    printf("[FILE] Recording to: %s  (%d bytes/sample: "
           "%d events × [%d-bit HDR + %d bytes CH])\n",
           filename, PAYLOAD_BYTES, NUM_EVENTS, EVENT_HDR * 8, CH_BYTES);
}

/* ══════════════════════════════════════════════════════════════════════
 * SPI
 * ══════════════════════════════════════════════════════════════════════ */

/** @brief Full-duplex SPI transfer. */
static int spi_transfer(int fd, uint8_t *tx, uint8_t *rx, uint32_t len)
{
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .speed_hz      = SPI_SPEED,
        .bits_per_word = SPI_BITS,
    };
    return ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
}

/** @brief Open and configure the SPI device. Returns fd or -1. */
static int spi_init(const char *device, uint8_t mode, uint8_t bits, uint32_t speed)
{
    int fd = open(device, O_RDWR);
    if (fd < 0) { perror("[ERROR] Open SPI"); return -1; }

    if (ioctl(fd, SPI_IOC_WR_MODE,          &mode)  < 0) goto err;
    if (ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits)  < 0) goto err;
    if (ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ,  &speed) < 0) goto err;
    return fd;

err:
    perror("[ERROR] SPI ioctl");
    close(fd);
    return -1;
}