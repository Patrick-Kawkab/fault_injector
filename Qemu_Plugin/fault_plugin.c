// =============================================================================
//  fault_plugin.c  —  QEMU TCG plugin for fault injection
//
//  Build:   see Makefile
//  Load:    -plugin ./fault_plugin.so,server=localhost:9001
//
//  Wire protocol (no JSON anywhere):
//    recv  :  read(sock,  &FaultDescriptor, sizeof(FaultDescriptor))
//    ack   :  write(sock, &ACK_READY,       1)
//    send  :  write(sock, &FaultResult,     sizeof(FaultResult))
//
//  Print levels:
//    [fault_plugin] install     — connection + descriptor handshake
//    [fault_plugin] vcpu_init   — vCPU initialised, register capture
//    [fault_plugin] tb_trans    — every TCG translation block
//    [fault_plugin] insn_exec   — trigger hit + injection only
//    [fault_plugin] mem_access  — sensor trigger hit + injection only
//    [fault_plugin] result      — final evaluation + result sent
// =============================================================================

#include <qemu-plugin.h>
#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>

#include "../FaultConfig.h"

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

#define ACK_READY ((uint8_t)0xAC)

// ============================================================================
//  Global state
// ============================================================================

static FaultDescriptor              g_fault;
static int                          g_sock            = -1;
static uint64_t                     g_insn_count      = 0;
static bool                         g_injected        = false;
static bool                         g_passed          = false;
static char                         g_server_host[64] = "127.0.0.1";
static int                          g_server_port     = 9001;
static struct qemu_plugin_register *g_pc_reg_handle   = NULL;

// Translation block counter for prints
static uint64_t g_tb_count = 0;

// ============================================================================
//  Name helpers for prints
// ============================================================================

static const char *fault_type_name(uint8_t ft)
{
    switch (ft) {
        case FAULT_MEMORY_CORRUPTION: return "memory_corruption";
        case FAULT_INSTRUCTION_SKIP:  return "instruction_skip";
        case FAULT_BIT_FLIP:          return "bit_flip";
        case FAULT_SET_PC:            return "set_pc";
        case FAULT_SENSOR_CORRUPTION: return "sensor_corruption";
        default:                      return "unknown";
    }
}

static const char *trigger_name(uint8_t tr)
{
    switch (tr) {
        case TRIGGER_PC:         return "pc";
        case TRIGGER_INSN_COUNT: return "insn_count";
        case TRIGGER_MEM_ACCESS: return "mem_access";
        default:                 return "unknown";
    }
}

// ============================================================================
//  TCP handshake
// ============================================================================

static bool connect_to_session(void)
{
    fprintf(stderr, "[fault_plugin] install: connecting to %s:%d\n",
            g_server_host, g_server_port);

    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        fprintf(stderr, "[fault_plugin] install: socket() failed\n");
        return false;
    }

    int flag = 1;
    setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &addr.sin_addr);

    for (int i = 0; i < 10; i++) {
        fprintf(stderr, "[fault_plugin] install: connect attempt %d/10...\n", i + 1);
        if (connect(g_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            fprintf(stderr, "[fault_plugin] install: connected to QEMUSession\n");
            return true;
        }
        usleep(200000);
    }

    fprintf(stderr, "[fault_plugin] install: FATAL — all connect attempts failed\n");
    close(g_sock);
    g_sock = -1;
    return false;
}

static bool recv_fault_descriptor(void)
{
    fprintf(stderr, "[fault_plugin] install: waiting for FaultDescriptor (%zu bytes)...\n",
            sizeof(g_fault));

    ssize_t n = read(g_sock, &g_fault, sizeof(g_fault));
    if (n != (ssize_t)sizeof(g_fault)) {
        fprintf(stderr, "[fault_plugin] install: short read (%zd/%zu bytes)\n",
                n, sizeof(g_fault));
        return false;
    }

    fprintf(stderr, "[fault_plugin] install: descriptor received\n");
    fprintf(stderr, "[fault_plugin] install:   fault_type     = %s (%u)\n",
            fault_type_name(g_fault.fault_type), g_fault.fault_type);
    fprintf(stderr, "[fault_plugin] install:   trigger        = %s (%u)\n",
            trigger_name(g_fault.trigger), g_fault.trigger);
    fprintf(stderr, "[fault_plugin] install:   target_addr    = 0x%08X\n", g_fault.target_addr);
    fprintf(stderr, "[fault_plugin] install:   inject_addr    = 0x%08X\n", g_fault.inject_addr);
    fprintf(stderr, "[fault_plugin] install:   injected_value = 0x%02X\n", g_fault.injected_value);
    fprintf(stderr, "[fault_plugin] install:   bit_pos        = %u\n",     g_fault.bit_pos);
    fprintf(stderr, "[fault_plugin] install:   new_pc         = 0x%08X\n", g_fault.new_pc);
    fprintf(stderr, "[fault_plugin] install:   sensor_addr    = 0x%08X\n", g_fault.sensor_addr);
    fprintf(stderr, "[fault_plugin] install:   target_count   = %" PRIu64 "\n", g_fault.target_count);
    fprintf(stderr, "[fault_plugin] install:   min_expected   = 0x%02X\n", g_fault.min_expected);
    fprintf(stderr, "[fault_plugin] install:   max_expected   = 0x%02X\n", g_fault.max_expected);

    uint8_t ack = ACK_READY;
    if (write(g_sock, &ack, 1) != 1) {
        fprintf(stderr, "[fault_plugin] install: WARNING — ACK write failed\n");
    } else {
        fprintf(stderr, "[fault_plugin] install: ACK sent (0xAC)\n");
    }
    return true;
}

// ============================================================================
//  Guest memory read/write  (QEMU 8.x GByteArray API)
// ============================================================================

static bool guest_read_u8(uint64_t vaddr, uint8_t *out)
{
    GByteArray *buf = g_byte_array_new();
    g_byte_array_set_size(buf, 1);
    bool ok = qemu_plugin_read_memory_vaddr(vaddr, buf, 1);
    if (ok) *out = buf->data[0];
    g_byte_array_free(buf, TRUE);
    return ok;
}

static bool guest_write_u8(uint64_t vaddr, uint8_t val)
{
    GByteArray *buf = g_byte_array_new();
    g_byte_array_append(buf, &val, 1);
    bool ok = qemu_plugin_write_memory_vaddr(vaddr, buf);
    g_byte_array_free(buf, TRUE);
    return ok;
}

// ============================================================================
//  Fault injection
// ============================================================================

static void do_memory_corruption(void)
{
    fprintf(stderr, "[fault_plugin] insn_exec: TRIGGER HIT"
            "  PC=0x%08X  insn_count=%" PRIu64 "\n",
            g_fault.target_addr, g_insn_count);
    fprintf(stderr, "[fault_plugin] insn_exec: injecting memory_corruption"
            "  addr=0x%08X  value=0x%02X\n",
            g_fault.inject_addr, g_fault.injected_value);

    if (guest_write_u8(g_fault.inject_addr, g_fault.injected_value)) {
        fprintf(stderr, "[fault_plugin] insn_exec: memory_corruption OK"
                "  wrote 0x%02X to 0x%08X\n",
                g_fault.injected_value, g_fault.inject_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] insn_exec: memory_corruption FAILED"
                "  write error at 0x%08X\n", g_fault.inject_addr);
    }
}

static void do_instruction_skip(void)
{
    fprintf(stderr, "[fault_plugin] insn_exec: TRIGGER HIT"
            "  PC=0x%08X  insn_count=%" PRIu64 "\n",
            g_fault.target_addr, g_insn_count);
    fprintf(stderr, "[fault_plugin] insn_exec: injecting instruction_skip"
            "  addr=0x%08X  replacing with Thumb NOP (0xBF00)\n",
            g_fault.inject_addr);

    // Thumb-2 NOP = 0xBF00 (little-endian)
    bool ok = guest_write_u8(g_fault.inject_addr,     0x00) &&
              guest_write_u8(g_fault.inject_addr + 1, 0xBF);
    if (ok) {
        fprintf(stderr, "[fault_plugin] insn_exec: instruction_skip OK"
                "  NOP written at 0x%08X\n", g_fault.inject_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] insn_exec: instruction_skip FAILED"
                "  write error at 0x%08X\n", g_fault.inject_addr);
    }
}

static void do_bit_flip(void)
{
    fprintf(stderr, "[fault_plugin] insn_exec: TRIGGER HIT"
            "  insn_count=%" PRIu64 "  target_count=%" PRIu64 "\n",
            g_insn_count, g_fault.target_count);
    fprintf(stderr, "[fault_plugin] insn_exec: injecting bit_flip"
            "  addr=0x%08X  bit=%u\n",
            g_fault.inject_addr, g_fault.bit_pos);

    uint8_t val;
    if (!guest_read_u8(g_fault.inject_addr, &val)) {
        fprintf(stderr, "[fault_plugin] insn_exec: bit_flip FAILED"
                "  read error at 0x%08X\n", g_fault.inject_addr);
        return;
    }
    uint8_t flipped = val ^ (uint8_t)(1u << g_fault.bit_pos);
    fprintf(stderr, "[fault_plugin] insn_exec: bit_flip"
            "  old=0x%02X  mask=0x%02X  new=0x%02X\n",
            val, (uint8_t)(1u << g_fault.bit_pos), flipped);

    if (guest_write_u8(g_fault.inject_addr, flipped)) {
        fprintf(stderr, "[fault_plugin] insn_exec: bit_flip OK"
                "  0x%02X -> 0x%02X at 0x%08X\n",
                val, flipped, g_fault.inject_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] insn_exec: bit_flip FAILED"
                "  write error at 0x%08X\n", g_fault.inject_addr);
    }
}

static void do_sensor_corruption(uint64_t vaddr)
{
    fprintf(stderr, "[fault_plugin] mem_access: TRIGGER HIT"
            "  vaddr=0x%08" PRIX64 "  sensor_addr=0x%08X"
            "  insn_count=%" PRIu64 "\n",
            vaddr, g_fault.sensor_addr, g_insn_count);
    fprintf(stderr, "[fault_plugin] mem_access: injecting sensor_corruption"
            "  spoofed_value=0x%02X\n", g_fault.injected_value);

    if (guest_write_u8(g_fault.sensor_addr, g_fault.injected_value)) {
        fprintf(stderr, "[fault_plugin] mem_access: sensor_corruption OK"
                "  wrote 0x%02X to 0x%08X\n",
                g_fault.injected_value, g_fault.sensor_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] mem_access: sensor_corruption FAILED"
                "  write error at 0x%08X\n", g_fault.sensor_addr);
    }
}

static void do_set_pc(void)
{
    fprintf(stderr, "[fault_plugin] insn_exec: TRIGGER HIT"
            "  insn_count=%" PRIu64 "  target_count=%" PRIu64 "\n",
            g_insn_count, g_fault.target_count);

    if (!g_pc_reg_handle) {
        fprintf(stderr, "[fault_plugin] insn_exec: set_pc FAILED"
                "  no PC register handle\n");
        return;
    }

    fprintf(stderr, "[fault_plugin] insn_exec: injecting set_pc"
            "  new_pc=0x%08X\n", g_fault.new_pc);

    uint32_t   new_pc = g_fault.new_pc;
    GByteArray *buf   = g_byte_array_new();
    g_byte_array_append(buf, (uint8_t*)&new_pc, sizeof(new_pc));
    bool ok = qemu_plugin_write_register(g_pc_reg_handle, buf);
    g_byte_array_free(buf, TRUE);

    if (ok) {
        fprintf(stderr, "[fault_plugin] insn_exec: set_pc OK"
                "  PC -> 0x%08X\n", new_pc);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] insn_exec: set_pc FAILED"
                "  register write error\n");
    }
}

// ============================================================================
//  Result evaluation + send
// ============================================================================

static void evaluate_result(void)
{
    fprintf(stderr, "[fault_plugin] result: evaluating...\n");
    fprintf(stderr, "[fault_plugin] result:   injected     = %s\n",
            g_injected ? "true" : "false");
    fprintf(stderr, "[fault_plugin] result:   fault_type   = %s\n",
            fault_type_name(g_fault.fault_type));

    if (g_fault.fault_type == FAULT_SET_PC ||
        g_fault.fault_type == FAULT_INSTRUCTION_SKIP) {
        g_passed = g_injected;
        fprintf(stderr, "[fault_plugin] result:   pass criteria = injection success only\n");
        fprintf(stderr, "[fault_plugin] result:   passed       = %s\n",
                g_passed ? "true" : "false");
        return;
    }

    uint8_t  actual = 0;
    uint32_t check  = (g_fault.fault_type == FAULT_SENSOR_CORRUPTION)
                      ? g_fault.sensor_addr : g_fault.inject_addr;

    fprintf(stderr, "[fault_plugin] result:   reading final value from 0x%08X\n", check);

    if (!guest_read_u8(check, &actual)) {
        fprintf(stderr, "[fault_plugin] result:   read failed — FAILED\n");
        g_passed = false;
        return;
    }

    fprintf(stderr, "[fault_plugin] result:   actual       = 0x%02X\n", actual);
    fprintf(stderr, "[fault_plugin] result:   expected     = [0x%02X, 0x%02X]\n",
            g_fault.min_expected, g_fault.max_expected);

    g_passed = g_injected &&
               actual >= g_fault.min_expected &&
               actual <= g_fault.max_expected;

    fprintf(stderr, "[fault_plugin] result:   in range     = %s\n",
            (actual >= g_fault.min_expected && actual <= g_fault.max_expected)
            ? "true" : "false");
    fprintf(stderr, "[fault_plugin] result:   passed       = %s\n",
            g_passed ? "true" : "false");
}

static void send_result(void)
{
    evaluate_result();

    FaultResult result;
    result.injected   = g_injected ? 1 : 0;
    result.passed     = g_passed   ? 1 : 0;
    result._pad[0]    = 0; result._pad[1] = 0;
    result._pad[2]    = 0; result._pad[3] = 0;
    result._pad[4]    = 0; result._pad[5] = 0;
    result.insn_count = g_insn_count;

    fprintf(stderr, "\n[fault_plugin] result: sending FaultResult (%zu bytes)...\n",
            sizeof(result));
    fprintf(stderr, "[fault_plugin] result:   injected   = %u\n", result.injected);
    fprintf(stderr, "[fault_plugin] result:   passed     = %u\n", result.passed);
    fprintf(stderr, "[fault_plugin] result:   insn_count = %" PRIu64 "\n", result.insn_count);

    ssize_t n = write(g_sock, &result, sizeof(result));
    if (n != (ssize_t)sizeof(result)) {
        fprintf(stderr, "[fault_plugin] result: send FAILED"
                "  short write (%zd/%zu bytes)\n", n, sizeof(result));
    } else {
        fprintf(stderr, "[fault_plugin] result: send OK  ->  %s\n",
                result.passed ? "PASSED" : "FAILED");
    }

    close(g_sock);
    g_sock = -1;
    fprintf(stderr, "[fault_plugin] result: socket closed\n");
}

// ============================================================================
//  TCG callbacks
// ============================================================================

static void vcpu_insn_exec_cb(unsigned int vcpu_idx, void *userdata)
{
    (void)vcpu_idx;
    if (g_injected) return;

    uint64_t pc = (uint64_t)(uintptr_t)userdata;
    g_insn_count++;

    switch (g_fault.trigger) {
    case TRIGGER_PC:
        if ((uint32_t)pc == g_fault.target_addr) {
            if      (g_fault.fault_type == FAULT_MEMORY_CORRUPTION) do_memory_corruption();
            else if (g_fault.fault_type == FAULT_INSTRUCTION_SKIP)  do_instruction_skip();
        }
        break;

    case TRIGGER_INSN_COUNT:
        if (g_insn_count == g_fault.target_count) {
            if      (g_fault.fault_type == FAULT_BIT_FLIP) do_bit_flip();
            else if (g_fault.fault_type == FAULT_SET_PC)   do_set_pc();
        }
        break;

    default: break;
    }
}

static void vcpu_mem_access_cb(unsigned int vcpu_idx,
                                qemu_plugin_meminfo_t info,
                                uint64_t vaddr, void *userdata)
{
    (void)vcpu_idx; (void)info; (void)userdata;
    if (g_injected) return;
    if ((uint32_t)vaddr == g_fault.sensor_addr)
        do_sensor_corruption(vaddr);
}

static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    size_t n_insns = qemu_plugin_tb_n_insns(tb);
    g_tb_count++;

    // Print TB start address from first instruction
    uint64_t tb_pc = 0;
    if (n_insns > 0) {
        struct qemu_plugin_insn *first = qemu_plugin_tb_get_insn(tb, 0);
        tb_pc = qemu_plugin_insn_vaddr(first);
    }

    fprintf(stderr, "[fault_plugin] tb_trans: TB #%" PRIu64
            "  start_pc=0x%08" PRIX64 "  n_insns=%zu\n",
            g_tb_count, tb_pc, n_insns);

    for (size_t i = 0; i < n_insns; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);

        fprintf(stderr, "[fault_plugin] tb_trans:   [%zu]  PC=0x%08" PRIX64, i, pc);

        // Highlight if this instruction is the trigger target
        if (g_fault.trigger == TRIGGER_PC && (uint32_t)pc == g_fault.target_addr)
            fprintf(stderr, "  <-- TRIGGER TARGET");

        fprintf(stderr, "\n");

        qemu_plugin_register_vcpu_insn_exec_cb(
            insn, vcpu_insn_exec_cb,
            QEMU_PLUGIN_CB_RW_REGS,
            (void*)(uintptr_t)pc);

        if (g_fault.trigger == TRIGGER_MEM_ACCESS) {
            qemu_plugin_register_vcpu_mem_cb(
                insn, vcpu_mem_access_cb,
                QEMU_PLUGIN_CB_RW_REGS,
                QEMU_PLUGIN_MEM_RW,
                NULL);
        }
    }
}

// ============================================================================
//  vCPU init — capture PC register handle for set_pc fault
// ============================================================================

static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_idx)
{
    (void)id;
    fprintf(stderr, "[fault_plugin] vcpu_init: vCPU %u initialised\n", vcpu_idx);

    if (g_fault.fault_type != FAULT_SET_PC) return;
    if (g_pc_reg_handle) return;

    fprintf(stderr, "[fault_plugin] vcpu_init: set_pc fault — scanning registers\n");

    GArray *regs = qemu_plugin_get_registers();
    if (!regs) {
        fprintf(stderr, "[fault_plugin] vcpu_init: get_registers() returned NULL\n");
        return;
    }

    fprintf(stderr, "[fault_plugin] vcpu_init: %u registers available\n", regs->len);

    for (guint i = 0; i < regs->len; i++) {
        qemu_plugin_reg_descriptor *desc =
            &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        fprintf(stderr, "[fault_plugin] vcpu_init:   reg[%u] = %s\n",
                i, desc->name ? desc->name : "(null)");

        if (desc->name &&
            (strcmp(desc->name, "pc") == 0 ||
             strcmp(desc->name, "PC") == 0)) {
            g_pc_reg_handle = desc->handle;
            fprintf(stderr, "[fault_plugin] vcpu_init: PC register handle captured"
                    "  name=%s  index=%u\n", desc->name, i);
            break;
        }
    }
    g_array_free(regs, TRUE);

    if (!g_pc_reg_handle)
        fprintf(stderr, "[fault_plugin] vcpu_init: WARNING — PC register not found\n");
}

static void plugin_atexit_cb(qemu_plugin_id_t id, void *userdata)
{
    (void)id; (void)userdata;
    fprintf(stderr, "\n[fault_plugin] atexit: QEMU exiting"
            "  total_tb=%" PRIu64 "  total_insn=%" PRIu64 "\n",
            g_tb_count, g_insn_count);
    send_result();
}

// ============================================================================
//  Plugin entry point
// ============================================================================

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t   id,
                                            const qemu_info_t *info,
                                            int                argc,
                                            char              *argv[])
{
    (void)info;

    fprintf(stderr, "\n[fault_plugin] ============================================\n");
    fprintf(stderr, "[fault_plugin] install: fault_plugin starting\n");

    // Parse "server=host:port" argument
    for (int i = 0; i < argc; i++) {
        fprintf(stderr, "[fault_plugin] install: arg[%d] = %s\n", i, argv[i]);
        if (strncmp(argv[i], "server=", 7) == 0) {
            const char *hp    = argv[i] + 7;
            const char *colon = strrchr(hp, ':');
            if (colon) {
                size_t hlen = (size_t)(colon - hp);
                if (hlen >= sizeof(g_server_host)) hlen = sizeof(g_server_host) - 1;
                memcpy(g_server_host, hp, hlen);
                g_server_host[hlen] = '\0';
                g_server_port = atoi(colon + 1);
            } else {
                strncpy(g_server_host, hp, sizeof(g_server_host) - 1);
            }
            fprintf(stderr, "[fault_plugin] install: server = %s:%d\n",
                    g_server_host, g_server_port);
        }
    }

    if (!connect_to_session()) {
        fprintf(stderr, "[fault_plugin] install: FATAL — cannot reach QEMUSession\n");
        return -1;
    }
    if (!recv_fault_descriptor()) {
        fprintf(stderr, "[fault_plugin] install: FATAL — failed to receive descriptor\n");
        return -1;
    }

    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    qemu_plugin_register_atexit_cb(id, plugin_atexit_cb, NULL);

    fprintf(stderr, "[fault_plugin] install: callbacks registered\n");
    fprintf(stderr, "[fault_plugin] install: ready — waiting for guest execution\n");
    fprintf(stderr, "[fault_plugin] ============================================\n\n");
    return 0;
}