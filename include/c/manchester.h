/*
    project: MIL1553
    purpose: manchester II bi-phase encode/decode of a full MIL-STD-1553B word (sync + 16-bit field + parity bit) to/from
             40 packed half-bits symbols

    realizes: LLR-1553-WORD-006, LLR-1553-WORD-007
    parent HLR: HLR-1553-WORD-004

    symbol layout (LLR-1553-WORD-006):
       - command/status sync: symbols 1,1,1,0,0,0
       - data sync: symbols 0,0,0,1,1,1
       - each of the 17 field+parity bits (MSB first): 1,0 for a logic one, 0,1 for a logic zero
       - total: 6 (sync) + 17*2 (field+parity) = 40 symbols

    symbols are packed 8 per byte into mil1553_symbols_t (5 bytes), most significant symbol first within the array, per
    design/bus_transport_contract.md 4

    MISRA C: 2012 notes:
        - no dynamic memory, no recursion, bounded loops only
*/

#ifndef MIL1553_MANCHESTER_H
#define MIL1553_MANCHESTER_H

#include <stdint.h>
#include <stdbool.h>
#include "mil1553_types.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /*
        implements: LLR-1553-WORD-006
        encodes a word (sync type, 16-bit field, parity bit) into 40 Manchester II half-bit symbols, packed per the layout above

        parameters:
            sync - MIL1553_SYNC_COMMAND_STATUS or MIL1553_SYNC_DATA; any other value (including MIL1553_SYNC_NONE) is
            a programming error; the function writes am all-zero symbols buffer and returns false in that case

            field - 16-bit information field
            parity - parity bit (only bit 0 examined)
            out_symbols - destination buffer, always fully written

        returns:
            true on success, false if sync was not a valid transmittable sync type
    */
    bool mil1553_manchester_encode(mil1553_sync_t sync, uint16_t field, uint8_t parity, mil1553_symbols_t out_symbols);

    /*
        implements: LLR-1553-WORD-007

        decodes 40 Manchester II half-bit symbols into sync type, 16-bit field, and parity bit; classifies sync by the exact pattern
        match against the command/status and data sync patterns; any other 6-symbol prefix yields MIL1553_SYNC_INVALID;
        reports a manchester violation when any of the 34 data/parity symbol-pairs (17 bits) is 0,0 or 1,1 (missing the required
        mid-bit transition)

        parameters:
            in_symbols     - source buffer (40 packed symbols)
            out_sync       - decoded sync type (always written)
            out_field      - decoded 16-bit field (valid only if no violation and sync is not INVALID; written regardless, best-effort)
            out_parity     - decoded parity bit (0 or 1; same validity caveat)
            out_violation  - set true if any data/parity bit cell lacked its required transition

        returns:
            true if sync was recognized (COMMAND_STATUS or DATA) and no Manchester violation was detected; false otherwise
            callers must still check *out_violation and *out_sync individually if they need to distinguish the failure season
            (this is what mil1553_word's ckassify_word, LLR-1553-WORD-008, does
    */
    bool manchester_decode(const mil1553_symbols_t in_symbols, mil1553_sync_t *out_sync, uint16_t *out_field,
                           uint8_t *out_parity, bool *out_violation);

#ifdef __cplusplus
}
#endif

#endif
