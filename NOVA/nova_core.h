#ifndef NOVA_CORE_H
#define NOVA_CORE_H

#include <stdint.h>

/* Advanced Software Architecture Definition: Result Types for Defensive Design */
typedef enum {
    NOVA_STATUS_OK = 0,
    NOVA_STATUS_HALT,
    NOVA_STATUS_ERR_INVALID_OPCODE,
    NOVA_STATUS_ERR_MEM_BOUNDS
} Nova_Result;

/* Fidelity Core State Representation - Completely Agnostic of Host OS */
typedef struct {
    uint16_t PC;        /* Program Counter */
    uint16_t AC[4];     /* Accumulators 0-3 */
    uint16_t SP;        /* Stack Pointer (For Eclipse extensions) */
    uint16_t SR;        /* Status Register / Flags */
} Nova_CPU_State;

/* Function pointer layout for the O(1) Flattened Dispatch Matrix */
typedef Nova_Result (*InstructionHandler)(Nova_CPU_State *state, uint16_t instr);

/* Exported Core Decoupled Execution Interface */
Nova_Result decode_and_execute(Nova_CPU_State *state, uint16_t instr);

#endif /* NOVA_CORE_H */
