/* Minimal SHA-256 (FIPS 180-4). Public-domain style, no dynamic allocation. */
#include "domain/sha256.h"
#include <string.h>

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const uint32_t K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
    0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
    0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
    0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
    0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
    0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
    0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
    0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
    0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
    0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  buf[64];
    uint32_t buflen;
} sha256_ctx;

static void sha256_transform(sha256_ctx *c, const uint8_t *p) {
    uint32_t w[64];
    uint32_t a, b, cc, d, e, f, g, h;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
               ((uint32_t)p[i * 4 + 2] << 8) | ((uint32_t)p[i * 4 + 3]);
    }
    for (i = 16; i < 64; i++) {
        uint32_t s0 = ROTR(w[i - 15], 7) ^ ROTR(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = ROTR(w[i - 2], 17) ^ ROTR(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    a = c->state[0]; b = c->state[1]; cc = c->state[2]; d = c->state[3];
    e = c->state[4]; f = c->state[5]; g = c->state[6]; h = c->state[7];

    for (i = 0; i < 64; i++) {
        uint32_t S1 = ROTR(e, 6) ^ ROTR(e, 11) ^ ROTR(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = ROTR(a, 2) ^ ROTR(a, 13) ^ ROTR(a, 22);
        uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
        uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = cc; cc = b; b = a; a = t1 + t2;
    }

    c->state[0] += a; c->state[1] += b; c->state[2] += cc; c->state[3] += d;
    c->state[4] += e; c->state[5] += f; c->state[6] += g; c->state[7] += h;
}

void sha256(const uint8_t *data, size_t len, uint8_t out[SHA256_DIGEST_SIZE]) {
    sha256_ctx c;
    size_t i;
    uint8_t pad[72];
    uint32_t padlen;
    uint64_t bits;

    c.state[0] = 0x6a09e667UL; c.state[1] = 0xbb67ae85UL;
    c.state[2] = 0x3c6ef372UL; c.state[3] = 0xa54ff53aUL;
    c.state[4] = 0x510e527fUL; c.state[5] = 0x9b05688cUL;
    c.state[6] = 0x1f83d9abUL; c.state[7] = 0x5be0cd19UL;
    c.bitlen = 0;
    c.buflen = 0;

    for (i = 0; i < len; i++) {
        c.buf[c.buflen++] = data[i];
        if (c.buflen == 64) {
            sha256_transform(&c, c.buf);
            c.bitlen += 512;
            c.buflen = 0;
        }
    }

    bits = c.bitlen + (uint64_t)c.buflen * 8;

    /* Build padding: 0x80, then zeros, then 64-bit big-endian length. */
    memset(pad, 0, sizeof(pad));
    pad[0] = 0x80;
    /* total appended so that (buflen + padlen) % 64 == 0, leaving 8 bytes for len */
    if (c.buflen < 56) {
        padlen = 56 - c.buflen;
    } else {
        padlen = 120 - c.buflen;
    }
    pad[padlen + 0] = (uint8_t)(bits >> 56);
    pad[padlen + 1] = (uint8_t)(bits >> 48);
    pad[padlen + 2] = (uint8_t)(bits >> 40);
    pad[padlen + 3] = (uint8_t)(bits >> 32);
    pad[padlen + 4] = (uint8_t)(bits >> 24);
    pad[padlen + 5] = (uint8_t)(bits >> 16);
    pad[padlen + 6] = (uint8_t)(bits >> 8);
    pad[padlen + 7] = (uint8_t)(bits);

    for (i = 0; i < (size_t)padlen + 8; i++) {
        c.buf[c.buflen++] = pad[i];
        if (c.buflen == 64) {
            sha256_transform(&c, c.buf);
            c.buflen = 0;
        }
    }

    for (i = 0; i < 8; i++) {
        out[i * 4 + 0] = (uint8_t)(c.state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(c.state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(c.state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)(c.state[i]);
    }
}

void sha256_to_hex(const uint8_t *in, size_t len, char *out) {
    static const char hexd[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < len; i++) {
        out[i * 2 + 0] = hexd[(in[i] >> 4) & 0xF];
        out[i * 2 + 1] = hexd[in[i] & 0xF];
    }
    out[len * 2] = '\0';
}
