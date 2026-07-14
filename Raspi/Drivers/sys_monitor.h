#ifndef SYS_MONITOR_H
#define SYS_MONITOR_H

typedef struct {
    double cpu_usage;    // Percentage (0-100)
    long total_ram;      // in KB
    long free_ram;       // in KB
    double ram_usage;    // Percentage (0-100)
} sys_metrics_t;

int get_sys_metrics(sys_metrics_t *metrics);

#endif // SYS_MONITOR_H
