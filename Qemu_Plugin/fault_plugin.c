// =============================================================================
//  fault_plugin.c  —  QEMU TCG plugin for fault injection
//
//  Build:   see Makefile
//  Load:    -plugin ./fault_plugin.so,server=localhost:9001
//
//  Lifecycle (mirrors QEMUSession's runCampaign):
//
//    qemu_plugin_install()
//      └─ parse "server=host:port" arg
//      └─ connect TCP to QEMUSession server
//      └─ recv JSON fault descriptor  (one line, '\n' terminated)
//      └─ send "READY\n" ACK
//      └─ register TCG callbacks based on fault_type / trigger
//
//    [QEMU translates + executes guest code]
//      TCG callbacks fire → trigger check → inject fault
//
//    qemu_plugin_atexit_cb()
//      └─ write campaign_result.json
//      └─ close socket
//
//  Fault types supported
//  ─────────────────────
//  memory_corruption  trigger=pc         : write injected_value to inject_addr
//                                          when PC == target_addr
//  instruction_skip   trigger=pc         : patch 2-byte Thumb NOP (0xBF00)
//                                          at inject_addr when PC == target_addr
//  bit_flip           trigger=insn_count : XOR bit at inject_addr when
//                                          instruction counter == target_count
//  set_pc             trigger=insn_count : redirect PC to new_pc when
//                                          instruction counter == target_count
//  sensor_corruption  trigger=mem_access : overwrite sensor_addr with
//                                          injected_value on any access
//
//  JSON descriptor fields (all sent by QEMUSession)
//  ─────────────────────────────────────────────────
//  fault_type      : string  (see above)
//  trigger         : "pc" | "insn_count" | "mem_access"
//  target_addr     : uint32  (for pc trigger)
//  inject_addr     : uint32  (address to write)
//  target_count    : uint64  (for insn_count trigger)
//  new_pc          : uint32  (for set_pc)
//  injected_value  : uint8
//  bit_pos         : uint8   (0-7, for bit_flip)
//  min_expected    : uint8
//  max_expected    : uint8
//  sensor_addr     : uint32  (for sensor_corruption)
//  result_file     : string  path for output JSON
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
 
QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;
 

// ============================================================================
//  Fault descriptor  (populated from JSON)
// ============================================================================

typedef enum {
    FAULT_MEMORY_CORRUPTION,
    FAULT_INSTRUCTION_SKIP,
    FAULT_BIT_FLIP,
    FAULT_SET_PC,
    FAULT_SENSOR_CORRUPTION,
    FAULT_UNKNOWN
} FaultType;
 
typedef enum {
    TRIGGER_PC,
    TRIGGER_INSN_COUNT,
    TRIGGER_MEM_ACCESS
} TriggerType;
 
typedef struct {
    FaultType   fault_type;
    TriggerType trigger;
    uint32_t    target_addr;
    uint32_t    inject_addr;
    uint64_t    target_count;
    uint32_t    new_pc;
    uint8_t     injected_value;
    uint8_t     bit_pos;
    uint8_t     min_expected;
    uint8_t     max_expected;
    uint32_t    sensor_addr;
    char        result_file[256];
} FaultDescriptor;

// ============================================================================
//  Global state
// ============================================================================

static FaultDescriptor g_fault;
static int             g_sock            = -1;
static uint64_t        g_insn_count      = 0;
static bool            g_injected        = false;
static bool            g_result          = false;
static char            g_server_host[64] = "127.0.0.1";
static int             g_server_port     = 9001;
 
// For set_pc: we need to capture the register handle at translate time
static struct qemu_plugin_register *g_pc_reg_handle = NULL;
// ============================================================================
//  Minimal JSON field extractor
// ============================================================================
 
static bool json_get_string(const char *src, const char *key,
                             char *dst, size_t max)
{
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char *p = strstr(src, pat);
    if (!p) return false;
    p += strlen(pat);
    while (*p == ' ') p++;
    if (*p == '"') {
        p++;
        size_t i = 0;
        while (*p && *p != '"' && i + 1 < max) dst[i++] = *p++;
        dst[i] = '\0';
    } else {
        size_t i = 0;
        while (*p && *p != ',' && *p != '}' && *p != ' ' && i + 1 < max)
            dst[i++] = *p++;
        dst[i] = '\0';
    }
    return dst[0] != '\0';
}
 
static uint64_t json_get_u64(const char *src, const char *key, uint64_t def)
{
    char buf[32];
    if (!json_get_string(src, key, buf, sizeof(buf))) return def;
    return (uint64_t)strtoull(buf, NULL, 0);
}
static uint32_t json_get_u32(const char *src, const char *key, uint32_t def)
{ return (uint32_t)json_get_u64(src, key, def); }
static uint8_t  json_get_u8 (const char *src, const char *key, uint8_t  def)
{ return (uint8_t) json_get_u64(src, key, def); }
 

// ============================================================================
//  Parse fault descriptor
// ============================================================================
 
static void parse_fault_descriptor(const char *json)
{
    char buf[64];
 
    json_get_string(json, "fault_type", buf, sizeof(buf));
    if      (strcmp(buf, "memory_corruption") == 0) g_fault.fault_type = FAULT_MEMORY_CORRUPTION;
    else if (strcmp(buf, "instruction_skip")  == 0) g_fault.fault_type = FAULT_INSTRUCTION_SKIP;
    else if (strcmp(buf, "bit_flip")          == 0) g_fault.fault_type = FAULT_BIT_FLIP;
    else if (strcmp(buf, "set_pc")            == 0) g_fault.fault_type = FAULT_SET_PC;
    else if (strcmp(buf, "sensor_corruption") == 0) g_fault.fault_type = FAULT_SENSOR_CORRUPTION;
    else                                             g_fault.fault_type = FAULT_UNKNOWN;
 
    json_get_string(json, "trigger", buf, sizeof(buf));
    if      (strcmp(buf, "pc")         == 0) g_fault.trigger = TRIGGER_PC;
    else if (strcmp(buf, "insn_count") == 0) g_fault.trigger = TRIGGER_INSN_COUNT;
    else if (strcmp(buf, "mem_access") == 0) g_fault.trigger = TRIGGER_MEM_ACCESS;
 
    g_fault.target_addr    = json_get_u32(json, "target_addr",    0);
    g_fault.inject_addr    = json_get_u32(json, "inject_addr",    0);
    g_fault.target_count   = json_get_u64(json, "target_count",   0);
    g_fault.new_pc         = json_get_u32(json, "new_pc",         0);
    g_fault.injected_value = json_get_u8 (json, "injected_value", 0);
    g_fault.bit_pos        = json_get_u8 (json, "bit_pos",        0);
    g_fault.min_expected   = json_get_u8 (json, "min_expected",   0);
    g_fault.max_expected   = json_get_u8 (json, "max_expected",   0);
    g_fault.sensor_addr    = json_get_u32(json, "sensor_addr",    0);
 
    if (!json_get_string(json, "result_file",
                         g_fault.result_file, sizeof(g_fault.result_file)))
        strncpy(g_fault.result_file, "./campaign_result.json",
                sizeof(g_fault.result_file) - 1);
}
 
// ============================================================================
//  TCP handshake
// ============================================================================
 
static bool connect_to_session(void)
{
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) return false;
 
    int flag = 1;
    setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
 
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_server_port);
    inet_pton(AF_INET, g_server_host, &addr.sin_addr);
 
    for (int i = 0; i < 10; i++) {
        if (connect(g_sock, (struct sockaddr*)&addr, sizeof(addr)) == 0)
            return true;
        usleep(200000);
    }
    close(g_sock); g_sock = -1;
    return false;
}
 
static bool recv_fault_descriptor(void)
{
    char buf[1024] = {0};
    int  pos = 0;
    while (pos < (int)sizeof(buf) - 1) {
        char c;
        if (read(g_sock, &c, 1) <= 0) break;
        if (c == '\n') break;
        buf[pos++] = c;
    }
    if (pos == 0) return false;
 
    fprintf(stderr, "[fault_plugin] descriptor: %s\n", buf);
    parse_fault_descriptor(buf);
 
    const char *ack = "READY\n";
    if (write(g_sock, ack, strlen(ack)) < 0)
        fprintf(stderr, "[fault_plugin] warning: ACK write failed\n");
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
    if (guest_write_u8(g_fault.inject_addr, g_fault.injected_value)) {
        fprintf(stderr, "[fault_plugin] memory_corruption: wrote 0x%02x to 0x%08x\n",
                g_fault.injected_value, g_fault.inject_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] memory_corruption: write failed at 0x%08x\n",
                g_fault.inject_addr);
    }
}
 
static void do_instruction_skip(void)
{
    // Thumb-2 NOP = 0xBF00 (little-endian: lo=0x00, hi=0xBF)
    bool ok = guest_write_u8(g_fault.inject_addr,     0x00) &&
              guest_write_u8(g_fault.inject_addr + 1, 0xBF);
    if (ok) {
        fprintf(stderr, "[fault_plugin] instruction_skip: NOP at 0x%08x\n",
                g_fault.inject_addr);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] instruction_skip: write failed\n");
    }
}
 
static void do_bit_flip(void)
{
    uint8_t val;
    if (!guest_read_u8(g_fault.inject_addr, &val)) {
        fprintf(stderr, "[fault_plugin] bit_flip: read failed\n");
        return;
    }
    uint8_t flipped = val ^ (uint8_t)(1u << g_fault.bit_pos);
    if (guest_write_u8(g_fault.inject_addr, flipped)) {
        fprintf(stderr, "[fault_plugin] bit_flip: 0x%02x->0x%02x at 0x%08x bit%u\n",
                val, flipped, g_fault.inject_addr, g_fault.bit_pos);
        g_injected = true;
    }
}
 
static void do_sensor_corruption(void)
{
    if (guest_write_u8(g_fault.sensor_addr, g_fault.injected_value)) {
        fprintf(stderr, "[fault_plugin] sensor_corruption: wrote 0x%02x to 0x%08x\n",
                g_fault.injected_value, g_fault.sensor_addr);
        g_injected = true;
    }
}
 
// set_pc: write new PC value via register handle captured at translate time
static void do_set_pc(void)
{
    if (!g_pc_reg_handle) {
        fprintf(stderr, "[fault_plugin] set_pc: no register handle captured\n");
        return;
    }
    // ARM little-endian: PC is 4 bytes
    uint32_t  new_pc = g_fault.new_pc;
    GByteArray *buf  = g_byte_array_new();
    g_byte_array_append(buf, (uint8_t*)&new_pc, sizeof(new_pc));
    bool ok = qemu_plugin_write_register(g_pc_reg_handle, buf);
    g_byte_array_free(buf, TRUE);
    if (ok) {
        fprintf(stderr, "[fault_plugin] set_pc: PC -> 0x%08x\n", new_pc);
        g_injected = true;
    } else {
        fprintf(stderr, "[fault_plugin] set_pc: register write failed\n");
    }
}
 
// ============================================================================
//  Result evaluation & output
// ============================================================================
 
static void evaluate_result(void)
{
    if (g_fault.fault_type == FAULT_SET_PC ||
        g_fault.fault_type == FAULT_INSTRUCTION_SKIP) {
        g_result = g_injected;
        return;
    }
    uint8_t  actual = 0;
    uint32_t check  = (g_fault.fault_type == FAULT_SENSOR_CORRUPTION)
                      ? g_fault.sensor_addr : g_fault.inject_addr;
    if (!guest_read_u8(check, &actual)) { g_result = false; return; }
    g_result = g_injected &&
               actual >= g_fault.min_expected &&
               actual <= g_fault.max_expected;
}
 
static void write_result_file(void)
{
    evaluate_result();
    FILE *f = fopen(g_fault.result_file, "w");
    if (!f) {
        fprintf(stderr, "[fault_plugin] cannot open %s\n", g_fault.result_file);
        return;
    }
    const char *ft = "unknown";
    switch (g_fault.fault_type) {
        case FAULT_MEMORY_CORRUPTION: ft = "memory_corruption"; break;
        case FAULT_INSTRUCTION_SKIP:  ft = "instruction_skip";  break;
        case FAULT_BIT_FLIP:          ft = "bit_flip";          break;
        case FAULT_SET_PC:            ft = "set_pc";            break;
        case FAULT_SENSOR_CORRUPTION: ft = "sensor_corruption"; break;
        default: break;
    }
    fprintf(f,
        "{\n"
        "    \"fault_type\":     \"%s\",\n"
        "    \"trigger_addr\":   \"0x%08X\",\n"
        "    \"inject_addr\":    \"0x%08X\",\n"
        "    \"injected_value\": %u,\n"
        "    \"min_expected\":   %u,\n"
        "    \"max_expected\":   %u,\n"
        "    \"insn_count\":     %" PRIu64 ",\n"
        "    \"injected\":       %s,\n"
        "    \"result\":         \"%s\"\n"
        "}\n",
        ft,
        g_fault.target_addr, g_fault.inject_addr,
        g_fault.injected_value,
        g_fault.min_expected, g_fault.max_expected,
        g_insn_count,
        g_injected ? "true" : "false",
        g_result   ? "PASSED" : "FAILED");
    fclose(f);
    fprintf(stderr, "[fault_plugin] result -> %s  (%s)\n",
            g_fault.result_file, g_result ? "PASSED" : "FAILED");
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
        do_sensor_corruption();
}
 
static void vcpu_tb_trans_cb(qemu_plugin_id_t id, struct qemu_plugin_tb *tb)
{
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
 
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t pc = qemu_plugin_insn_vaddr(insn);
 
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
//  vCPU init callback — capture PC register handle for set_pc fault
// ============================================================================
 
static void vcpu_init_cb(qemu_plugin_id_t id, unsigned int vcpu_idx)
{
    (void)id; (void)vcpu_idx;
 
    if (g_fault.fault_type != FAULT_SET_PC) return;
    if (g_pc_reg_handle) return;   // already captured on a previous vCPU
 
    GArray *regs = qemu_plugin_get_registers();
    if (!regs) return;
 
    for (guint i = 0; i < regs->len; i++) {
        qemu_plugin_reg_descriptor *desc =
            &g_array_index(regs, qemu_plugin_reg_descriptor, i);
        // ARM: look for "pc" or "PC"
        if (desc->name &&
            (strcmp(desc->name, "pc") == 0 ||
             strcmp(desc->name, "PC") == 0)) {
            g_pc_reg_handle = desc->handle;
            fprintf(stderr, "[fault_plugin] set_pc: captured PC handle (%s)\n",
                    desc->name);
            break;
        }
    }
    g_array_free(regs, TRUE);
 
    if (!g_pc_reg_handle)
        fprintf(stderr, "[fault_plugin] set_pc: WARNING — PC register not found\n");
}
 
static void plugin_atexit_cb(qemu_plugin_id_t id, void *userdata)
{
    (void)id; (void)userdata;
    write_result_file();
    if (g_sock >= 0) { close(g_sock); g_sock = -1; }
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
 
    for (int i = 0; i < argc; i++) {
        if (strncmp(argv[i], "server=", 7) == 0) {
            const char *hp    = argv[i] + 7;
            const char *colon = strrchr(hp, ':');
            if (colon) {
                size_t hlen = (size_t)(colon - hp);
                if (hlen >= sizeof(g_server_host)) hlen = sizeof(g_server_host)-1;
                memcpy(g_server_host, hp, hlen);
                g_server_host[hlen] = '\0';
                g_server_port = atoi(colon + 1);
            } else {
                strncpy(g_server_host, hp, sizeof(g_server_host)-1);
            }
        }
    }
 
    fprintf(stderr, "[fault_plugin] connecting to %s:%d\n",
            g_server_host, g_server_port);
 
    if (!connect_to_session()) {
        fprintf(stderr, "[fault_plugin] FATAL: cannot reach QEMUSession\n");
        return -1;
    }
    if (!recv_fault_descriptor()) {
        fprintf(stderr, "[fault_plugin] FATAL: no fault descriptor\n");
        return -1;
    }
 
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans_cb);
    qemu_plugin_register_vcpu_init_cb(id, vcpu_init_cb);
    qemu_plugin_register_atexit_cb(id, plugin_atexit_cb, NULL);
 
    fprintf(stderr, "[fault_plugin] installed  fault=%d  trigger=%d\n",
            (int)g_fault.fault_type, (int)g_fault.trigger);
    return 0;
}
