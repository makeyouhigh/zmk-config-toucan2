/* SPDX-License-Identifier: MIT */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include "tps43_trace.h"

#define TRACE_CAPACITY 1536
#define TRACE_MS 10000U

static struct tps43_trace_record records[TRACE_CAPACITY];
static struct k_spinlock trace_lock;
static uint32_t started_ms;
static uint16_t count;
static uint16_t cursor;
static bool capturing;
static bool touching;
static bool full;

/* Caller holds trace_lock. Unsigned subtraction tolerates uptime wrap. */
static void expire(uint32_t now) {
    if (capturing && now - started_ms >= TRACE_MS) {
        capturing = false;
    }
}

void tps43_trace_record(const struct tps43_trace_record *record, bool contact) {
    k_spinlock_key_t key = k_spin_lock(&trace_lock);
    touching = contact;
    expire(k_uptime_get_32());
    if (capturing && record->sample_ms - started_ms < TRACE_MS) {
        records[count++] = *record;
        if (count == TRACE_CAPACITY) {
            full = true;
            capturing = false;
        }
    }
    k_spin_unlock(&trace_lock, key);
}

static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < length; i++) {
        hash = (hash ^ data[i]) * 16777619U;
    }
    return hash;
}

/* Stop-and-wait protocol: the host must consume one complete reply before
 * sending another command. Replies are <= 180 bytes, well below the 1024-byte
 * CDC TX ring. Thus poll_out never overruns that ring in this protocol.
 * There is no console or log backend on this dedicated UART. */
static void reply(const struct device *uart, const char *line) {
    uint32_t dtr = 0;
    if (uart_line_ctrl_get(uart, UART_LINE_CTRL_DTR, &dtr) || !dtr) {
        return;
    }
    for (const char *p = line; *p; p++) {
        uart_poll_out(uart, (unsigned char)*p);
    }
}

static void trace_thread(void *a, void *b, void *c) {
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
    const struct device *uart = DEVICE_DT_GET(DT_NODELABEL(toucan_trace_uart));
    /* ZMK's src/usb.c enables USB_DEVICE_STACK once; do not call usb_enable. */
    if (!device_is_ready(uart)) {
        return;
    }
    for (;;) {
        unsigned char command;
        if (uart_poll_in(uart, &command)) {
            k_sleep(K_MSEC(10));
            continue;
        }
        char line[180];
        struct tps43_trace_record record;
        bool have_record = false;
        uint16_t index = 0;
        k_spinlock_key_t key = k_spin_lock(&trace_lock);
        uint32_t now = k_uptime_get_32();
        expire(now);
        if (command == 'A') {
            if (capturing || touching) {
                snprintk(line, sizeof(line), "BUSY\n");
            } else {
                count = cursor = 0;
                full = false;
                started_ms = now;
                capturing = true;
                snprintk(line, sizeof(line), "ARM,v12,1,%u,%u\n", started_ms, TRACE_MS);
            }
        } else if (command == 'S') {
            snprintk(line, sizeof(line), "STATUS,v12,1,%u,%u,%u,%u,%u,%u,%u\n",
                     capturing, touching, count, full, started_ms, now,
                     (unsigned)sizeof(record));
        } else if (command == 'D') {
            if (capturing || touching) {
                snprintk(line, sizeof(line), "BUSY\n");
            } else {
                cursor = 0;
                snprintk(line, sizeof(line), "DATA,v12,1,%u,%u,%u,%u\n",
                         count, full, started_ms, (unsigned)sizeof(record));
            }
        } else if (command == 'N') {
            if (capturing || touching) {
                snprintk(line, sizeof(line), "BUSY\n");
            } else if (cursor >= count) {
                snprintk(line, sizeof(line), "END,%u\n", count);
            } else {
                index = cursor++;
                record = records[index];
                have_record = true;
            }
        } else {
            snprintk(line, sizeof(line), "ERROR,command\n");
        }
        k_spin_unlock(&trace_lock, key);
        if (have_record) {
            static const char hex[] = "0123456789abcdef";
            const uint8_t *bytes = (const uint8_t *)&record;
            size_t offset = (size_t)snprintk(line, sizeof(line), "R,%u,", index);
            for (size_t i = 0; i < sizeof(record); i++) {
                line[offset++] = hex[bytes[i] >> 4];
                line[offset++] = hex[bytes[i] & 15];
            }
            snprintk(line + offset, sizeof(line) - offset, ",%08x\n",
                     checksum(bytes, sizeof(record)));
        }
        reply(uart, line);
        /* Even a malformed/flooding host cannot monopolize a CPU. */
        k_sleep(K_MSEC(1));
    }
}
K_THREAD_DEFINE(toucan_trace_thread, 2048, trace_thread, NULL, NULL, NULL, 12, 0, 1000);
