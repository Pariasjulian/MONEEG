#include "sys_monitor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static long long prev_user, prev_nice, prev_system, prev_idle, prev_iowait, prev_irq, prev_softirq, prev_steal;

int get_sys_metrics(sys_metrics_t *metrics) {
    if (!metrics) return -1;

    // --- CPU Usage ---
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp) return -1;

    long long user, nice, system, idle, iowait, irq, softirq, steal;
    char buf[256];
    if (fgets(buf, sizeof(buf), fp)) {
        sscanf(buf, "cpu %lld %lld %lld %lld %lld %lld %lld %lld",
               &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal);
    }
    fclose(fp);

    long long prev_total = prev_user + prev_nice + prev_system + prev_idle + prev_iowait + prev_irq + prev_softirq + prev_steal;
    long long current_total = user + nice + system + idle + iowait + irq + softirq + steal;

    long long total_diff = current_total - prev_total;
    long long idle_diff = idle - prev_idle;

    if (total_diff > 0) {
        metrics->cpu_usage = 100.0 * (1.0 - (double)idle_diff / total_diff);
    } else {
        metrics->cpu_usage = 0.0;
    }

    prev_user = user;
    prev_nice = nice;
    prev_system = system;
    prev_idle = idle;
    prev_iowait = iowait;
    prev_irq = irq;
    prev_softirq = softirq;
    prev_steal = steal;

    // --- RAM Usage ---
    fp = fopen("/proc/meminfo", "r");
    if (!fp) return -1;

    long mem_total = 0, mem_free = 0, mem_available = 0;
    while (fgets(buf, sizeof(buf), fp)) {
        if (sscanf(buf, "MemTotal: %ld kB", &mem_total) == 1) continue;
        if (sscanf(buf, "MemFree: %ld kB", &mem_free) == 1) continue;
        if (sscanf(buf, "MemAvailable: %ld kB", &mem_available) == 1) break;
    }
    fclose(fp);

    metrics->total_ram = mem_total;
    metrics->free_ram = mem_available; // Available is better than Free usually
    if (mem_total > 0) {
        metrics->ram_usage = 100.0 * (1.0 - (double)mem_available / mem_total);
    } else {
        metrics->ram_usage = 0.0;
    }

    return 0;
}
