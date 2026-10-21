/*
    project: MIL1553
    purpose: SHA-256 and scenario-file signature check for the error injection framework (TEST BUILD ONLY)

    realizes: LLR-1553-ERR-007
    parent HLR: HLR-1553-ERR-005
*/

#ifndef MIL1553_CONFIG_SIGNATURE_H
#define MIL1553_CONFIG_SIGNATURE_H

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "config_signature is part of the error injection framework and must not be built in a release configuration (LLR-1553-ERR-005)"
#endif

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ERRINJECT_SIG_OK = 0,
    ERRINJECT_SIG_MISSING = 1, // signature file absent/unreadable
    ERRINJECT_SIG_MISMATCH = 2 // digest differs or file malformed
} errinject_sig_result_t;

// implements: LLR-1553-ERR-007; standard SHA-256 (FIPS 180-4)
void errinject_sha256(const uint8_t *data, uint32_t length, uint8_t digest[32]);

/*
    implements: LLR-1553-ERR-007

    computes SHA-256 over data and compares it with the 64-hex-digit value at the start of the file sig_path (extra text after the digest, such as the file name written
    by sha256sum, is ignored); the comparison is done on the bytes already in memory, so the bytes that are hased are exactly the bytes the caller goes on to parse 
    (no hash then reread window)
*/
errinject_sig_result_t errinject_check_signature(const uint8_t *data, uint32_t length, const char *sig_path);

#ifdef __cplusplus
}
#endif

#endif