#ifndef FAULT_CONFIG_H
#define FAULT_CONFIG_H

// =============================================================================
//  FaultConfig.h  —  shared binary protocol between QEMUSession and fault_plugin
//
//  This header is included by both C++ (QEMUSession) and C (fault_plugin).
//  No JSON anywhere. The structs are sent over TCP as raw binary.
//
//  Wire protocol:
//    QEMUSession  →  plugin  :  write(sock, &FaultConfig,  sizeof(FaultConfig))
//    plugin       →  session :  write(sock, &FaultResult,  sizeof(FaultResult))
//
//  IMPORTANT: both sides must be compiled for the same target/host so that
//  struct layout and endianness match. Add __attribute__((packed)) if needed.
// =============================================================================

#include <stdint.h>
#include <stdbool.h>

// ── Fault type ────────────────────────────────────────────────────────────────
typedef enum {
    FAULT_MEMORY_CORRUPTION  = 0,
    FAULT_INSTRUCTION_SKIP   = 1,
    FAULT_BIT_FLIP           = 2,
    FAULT_SET_PC             = 3,
    FAULT_SENSOR_CORRUPTION  = 4,
    FAULT_UNKNOWN            = 255
} FaultType;

// ── Trigger condition ─────────────────────────────────────────────────────────
typedef enum {
    TRIGGER_PC         = 0,   // fire when PC == target_addr
    TRIGGER_INSN_COUNT = 1,   // fire when instruction counter == target_count
    TRIGGER_MEM_ACCESS = 2    // fire on any access to sensor_addr
} TriggerType;

// ── Fault descriptor — sent from QEMUSession to plugin ───────────────────────
//
//  Fields used per fault type:
//
//  memory_corruption  : target_addr, inject_addr, injected_value,
//                       min_expected, max_expected
//  instruction_skip   : target_addr, inject_addr
//  bit_flip           : target_count, inject_addr, bit_pos,
//                       min_expected, max_expected
//  set_pc             : target_count, new_pc
//  sensor_corruption  : sensor_addr, inject_addr, injected_value,
//                       min_expected, max_expected
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint8_t   fault_type;       // FaultType  (use uint8_t for fixed wire size)
    uint8_t   trigger;          // TriggerType
    uint8_t   injected_value;   // value to write  (memory/sensor corruption)
    uint8_t   bit_pos;          // bit index 0-7   (bit_flip)
    uint8_t   min_expected;     // pass range low
    uint8_t   max_expected;     // pass range high
    uint8_t   _pad[2];          // explicit padding — keeps layout predictable
    uint32_t  target_addr;      // PC trigger address
    uint32_t  inject_addr;      // address to corrupt / flip
    uint32_t  new_pc;           // redirect target   (set_pc)
    uint32_t  sensor_addr;      // address to watch  (sensor_corruption)
    uint64_t  target_count;     // instruction count trigger
} FaultDescriptor;

// ── Result — sent from plugin back to QEMUSession ────────────────────────────
//
//  main merges this with the original FaultConfig to build campaign_result.json
// ─────────────────────────────────────────────────────────────────────────────
typedef struct {
    uint8_t   injected;         // 1 = fault was successfully injected
    uint8_t   passed;           // 1 = result within expected range
    uint8_t   _pad[6];          // padding to 16 bytes total
    uint64_t  insn_count;       // instruction count at time of injection
} FaultResult;

#endif // FAULT_CONFIG_H