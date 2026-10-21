/*
    project: MIL1553
    purpose: odd parity computation and checking over a 16-bit information field

    realizes: LLR-1553-WORD-005
    parent HLR: HLR-1553-WORD-005

    MISRA C:2012 notes>
        -- pure functions, no state, no dynamic memory 
        -- fixed-width types only
*/

#ifndef MIL1553_CRC_PARITY_H
#define MIL1553_CRC_PARITY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
    #endif

    /* 
        implements: LLR-1553-WORD-005
        computes the parity bit for a 16-bit information field such that the total numbers in one-bits in (parity, field) is odd

        parameters: 
            field - the 16-bit information field (command, status, or data field)

        returns:
            0 or 1, the parity bit value making the total ones-count odd
    */
   uint8_t crc_parity_compute(uint16_t field);

    /* 
        implements: LLR-1553-WORD-005
        checks the parity bit for a 16-bit information field such that the total numbers in one-bits in (parity, field) is odd

        parameters: 
            field - the 16-bit information field (command, status, or data field)
            parity - the parity bit value to check against

        returns:
            true if the parity is correct (total ones-count is odd), false otherwise
    */
   bool crc_parity_check(uint16_t field, uint8_t received_parity);

   #ifdef __cplusplus
}
#endif

#endif