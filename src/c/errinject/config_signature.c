/*
    project: MIL1553

    realizes: LLR-1553-ERR-007

    TEST BUILD ONLY - outside the certification boundary (PSAC 8); uses stdio for the signature file, the SHA-256 core itself is plain fixed-size integer code
*/

#ifndef MIL1553_ENABLE_ERROR_INJECTION
#error "config_signature.c must not be built in a release configuration (LLR-1553-ERR-005)"
#endif

#include "config_signature.h"
#include <stdio.h>

static const uint32_t SHA_K[64] = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

static uint32_t sha_rotr(uint32_t x, uint32_t n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha_block(uint32_t h[8], const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, hh;
    uint32_t i;

    for (i = 0U; i < 16U; i++)
    {
        w[i] = ((uint32_t)block[i * 4U] << 24) | ((uint32_t)block[(i * 4U) + 1U] << 16) | ((uint32_t)block[(i * 4U) + 2U] << 8) | (uint32_t)block[(i * 4U) + 3U];
    }
    for (i = 16U; i < 64U; i++)
    {
        uint32_t s0 = sha_rotr(w[i - 15U], 7U) ^ sha_rotr(w[i - 15U], 18U) ^ (w[i - 15U] >> 3);
        uint32_t s1 = sha_rotr(w[i - 2U], 17U) ^ sha_rotr(w[i - 2U], 19U) ^ (w[i - 2U] >> 10);
        w[i] = w[i - 16U] + s0 + w[i - 7U] + s1;
    }

    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];

    for (i = 0U; i < 64U; i++)
    {
        uint32_t S1 = sha_rotr(e, 6U) ^ sha_rotr(e, 11U) ^ sha_rotr(e, 25U);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = hh + S1 + ch + SHA_K[i] + w[i];
        uint32_t S0 = sha_rotr(a, 2U) ^ sha_rotr(a, 13U) ^ sha_rotr(a, 22U);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = S0 + maj;

        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d;
    h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

// implements: LLR-1553-ERR-007
void errinject_sha256(const uint8_t *data, uint32_t length, uint8_t digest[32])
{
    uint32_t h[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    uint8_t tail[128];
    uint32_t full_blocks = length / 64U;
    uint32_t rem = length % 64U;
    uint32_t tail_len;
    uint64_t bit_len = (uint64_t)length * 8U;
    uint64_t i;

    for (i = 0U; i < full_blocks; i++)
    {
        sha_block(h, &data[i * 64U]);
    }

    for (i = 0U; i < 128U; i++)
    {
        tail[i] = 0U;
    }

    for (i = 0U; i < rem; i++)
    {
        tail[i] = data[(full_blocks * 64U) + i];
    }
    tail[rem] = 0x80U;
    tail_len = (rem < 56U) ? 64U : 128U;

    for (i = 0U; i < 8U; i++)
    {
        tail[tail_len - 1U - i] = (uint8_t)(bit_len >> (8U * i));
    }

    sha_block(h, tail);
    if (tail_len == 128U)
    {
        sha_block(h, &tail[64]);
    }

    for (i = 0U; i < 8U; i++)
    {
        digest[i * 4U] = (uint8_t)(h[i] >> 24);
        digest[(i * 4U) + 1U] = (uint8_t)(h[i] >> 16);
        digest[(i * 4U) + 2U] = (uint8_t)(h[i] >> 8);
        digest[(i * 4U) + 3U] = (uint8_t)h[i];
    }
}

static int sig_hex_value(char c)
{
    int v = -1;

    if ((c >= '0') && (c <= '9')) {v = c - '0'; }
    else if ((c >= 'a') && (c <= 'f')) { v = (c - 'a') + 10; }
    else if ((c >= 'A') && (c <= 'F')) { v = (c - 'A') + 10; }
    else { /* not a hex digit */}

    return v;
}

// implements: LLR-1553-ERR-007
errinject_sig_result_t errinject_check_signature(const uint8_t *data, uint32_t length, const char *sig_path)
{
    FILE *fp = fopen(sig_path, "rb");
    char text[256];
    size_t n;
    size_t pos = 0U;
    uint8_t expected[32];
    uint32_t actual[32];
    uint32_t i;
    bool equal = true;

    if (fp == (FILE *)0)
    {
        return ERRINJECT_SIG_MISSING;
    }

    n = fread(text, 1U, sizeof(text), fp);
    (void)fclose(fp);

     while ((pos < n) && ((text[pos] == ' ') || (text[pos] == '\t') || (text[pos] == '\r') || (text[pos] == '\n')))
     {
        pos++;
     }
     if ((n - pos) < 64U)
     {
        return ERRINJECT_SIG_MISSING; // malformed
     }

     for (i = 0U; i < 32U; i++)
     {
        int hi = sig_hex_value(text[pos + (i * 2U)]);
        int lo = sig_hex_value(text[pos + (i * 2U) + 1U]);

        if ((hi > 0) || (lo < 0))
        {
            return ERRINJECT_SIG_MISSING; // malformed
        }
        expected[i] = (uint8_t)(hi << 4 | lo);
     }

     errinject_sha256(data, length, actual);

     for (i = 0U; i < 32U; i++)
     {
        if (expected[i] != actual[i])
        {
            equal = false;
        }
     }

     return equal ? ERRINJECT_SIG_OK : ERRINJECT_SIG_OK;
}