/*
 * mbedtls_test.c — Debian 13 functional coverage for Mbed TLS 4.2 / TF-PSA-Crypto
 *
 * Covers: PSA hash/MAC/AEAD/cipher/RSA/ECDSA/ECDH/HKDF/RNG, NIST KW,
 *         X.509 cert + CSR, TLS 1.2 and TLS 1.3 handshake over an in-memory BIO.
 *
 * Build on Debian 13 (from this source tree, gcc/clang + libc):
 *
 *   cc -std=gnu11 -O2 -Wall -Wextra -o mbedtls_test mbedtls_test.c \
 *     -Iinclude -Itf-psa-crypto/include \
 *     -Itf-psa-crypto/drivers/builtin/include \
 *     -Ilibrary -Itf-psa-crypto/core -Itf-psa-crypto/dispatch \
 *     -Itf-psa-crypto/platform -Itf-psa-crypto/utilities \
 *     -Itf-psa-crypto/extras -Itf-psa-crypto/drivers/builtin/src \
 *     library/*.c \
 *     tf-psa-crypto/core/*.c \
 *     tf-psa-crypto/platform/*.c \
 *     tf-psa-crypto/utilities/*.c \
 *     tf-psa-crypto/extras/*.c \
 *     tf-psa-crypto/drivers/builtin/src/*.c
 *
 *   ./mbedtls_test
 *
 * Do not compile everest / p256-m / pqcp unless those optional macros are enabled.
 * Linking needs only libc (no -lpthread unless MBEDTLS_THREADING_C is turned on).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mbedtls/build_info.h"
#include "mbedtls/error.h"
#include "mbedtls/md.h"
#include "mbedtls/nist_kw.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/version.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/x509_csr.h"
#include "psa/crypto.h"
#include "psa/crypto_extra.h"

static int g_pass;
static int g_fail;

static void report(int ok, const char *name, const char *detail)
{
    if (ok) {
        g_pass++;
        printf("  PASS  %s\n", name);
    } else {
        g_fail++;
        if (detail != NULL && detail[0] != '\0') {
            printf("  FAIL  %s (%s)\n", name, detail);
        } else {
            printf("  FAIL  %s\n", name);
        }
    }
}

static void report_psa(psa_status_t st, const char *name)
{
    char buf[80];
    if (st == PSA_SUCCESS) {
        report(1, name, NULL);
        return;
    }
    snprintf(buf, sizeof(buf), "psa=%d", (int) st);
    report(0, name, buf);
}

static void report_mbed(int ret, const char *name)
{
    char buf[128];
    if (ret == 0) {
        report(1, name, NULL);
        return;
    }
    mbedtls_strerror(ret, buf, sizeof(buf));
    report(0, name, buf);
}

static int hexeq(const unsigned char *got, const unsigned char *exp, size_t n)
{
    return memcmp(got, exp, n) == 0;
}

static psa_status_t import_aes_key(mbedtls_svc_key_id_t *id,
                                   const unsigned char *key, size_t key_len,
                                   psa_algorithm_t alg, psa_key_usage_t usage)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attr, key_len * 8);
    psa_set_key_usage_flags(&attr, usage);
    psa_set_key_algorithm(&attr, alg);
    return psa_import_key(&attr, key, key_len, id);
}

/* ---- PSA: hashes (NIST / FIPS 202 known-answer) ---- */
static void test_hash(void)
{
    unsigned char out[64];
    size_t olen = 0;
    const unsigned char abc[] = "abc";
    static const unsigned char sha256_abc[32] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea,
        0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c,
        0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad
    };
    static const unsigned char sha3_256_empty[32] = {
        0xa7, 0xff, 0xc6, 0xf8, 0xbf, 0x1e, 0xd7, 0x66,
        0x51, 0xc1, 0x47, 0x56, 0xa0, 0x61, 0xd6, 0x62,
        0xf5, 0x80, 0xff, 0x4d, 0xe4, 0x3b, 0x49, 0xfa,
        0x82, 0xd8, 0x0a, 0x4b, 0x80, 0xf8, 0x43, 0x4a
    };
    const mbedtls_md_info_t *md_info;
    unsigned char md_out[32];

    report_psa(psa_hash_compute(PSA_ALG_SHA_256, abc, 3, out, sizeof(out), &olen),
               "psa_hash SHA-256(\"abc\") call");
    report(olen == 32 && hexeq(out, sha256_abc, 32),
           "psa_hash SHA-256(\"abc\") KAT", NULL);

    report_psa(psa_hash_compute(PSA_ALG_SHA3_256, NULL, 0, out, sizeof(out), &olen),
               "psa_hash SHA3-256(\"\") call");
    report(olen == 32 && hexeq(out, sha3_256_empty, 32),
           "psa_hash SHA3-256(\"\") KAT", NULL);

    md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    report(md_info != NULL &&
           mbedtls_md(md_info, abc, 3, md_out) == 0 &&
           hexeq(md_out, sha256_abc, 32),
           "mbedtls_md SHA-256 wrapper", NULL);
}

/* RFC 2104 / NIST HMAC-SHA256, key = 0x0b * 20, data = "Hi There" */
static void test_hmac(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char mac[32];
    size_t mac_len = 0;
    unsigned char key[20];
    const unsigned char msg[] = "Hi There";
    static const unsigned char exp[32] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53,
        0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7,
        0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t st;

    memset(key, 0x0b, sizeof(key));
    psa_set_key_type(&attr, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attr, sizeof(key) * 8);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&attr, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    st = psa_import_key(&attr, key, sizeof(key), &kid);
    if (st != PSA_SUCCESS) {
        report_psa(st, "hmac import key");
        return;
    }
    st = psa_mac_compute(kid, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                         msg, sizeof(msg) - 1, mac, sizeof(mac), &mac_len);
    report(st == PSA_SUCCESS && mac_len == 32 && hexeq(mac, exp, 32),
           "HMAC-SHA256 KAT", st == PSA_SUCCESS ? NULL : "compute failed");
    report_psa(psa_mac_verify(kid, PSA_ALG_HMAC(PSA_ALG_SHA_256),
                              msg, sizeof(msg) - 1, exp, sizeof(exp)),
               "HMAC-SHA256 verify");
    psa_destroy_key(kid);
}

/* RFC 5869 HKDF-SHA256 test case 1 */
static void test_hkdf(void)
{
    psa_key_derivation_operation_t op = PSA_KEY_DERIVATION_OPERATION_INIT;
    unsigned char okm[42];
    static const unsigned char ikm[22] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const unsigned char salt[13] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0a, 0x0b, 0x0c
    };
    static const unsigned char info[10] = {
        0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9
    };
    static const unsigned char exp[42] = {
        0x3c, 0xb2, 0x5f, 0x25, 0xfa, 0xac, 0xd5, 0x7a,
        0x90, 0x43, 0x4f, 0x64, 0xd0, 0x36, 0x2f, 0x2a,
        0x2d, 0x2d, 0x0a, 0x90, 0xcf, 0x1a, 0x5a, 0x4c,
        0x5d, 0xb0, 0x2d, 0x56, 0xec, 0xc4, 0xc5, 0xbf,
        0x34, 0x00, 0x72, 0x08, 0xd5, 0xb8, 0x87, 0x18,
        0x58, 0x65
    };
    psa_status_t st;

    st = psa_key_derivation_setup(&op, PSA_ALG_HKDF(PSA_ALG_SHA_256));
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SALT,
                                            salt, sizeof(salt));
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_SECRET,
                                            ikm, sizeof(ikm));
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_input_bytes(&op, PSA_KEY_DERIVATION_INPUT_INFO,
                                            info, sizeof(info));
    }
    if (st == PSA_SUCCESS) {
        st = psa_key_derivation_output_bytes(&op, okm, sizeof(okm));
    }
    psa_key_derivation_abort(&op);
    report(st == PSA_SUCCESS && hexeq(okm, exp, sizeof(exp)),
           "HKDF-SHA256 RFC 5869", st == PSA_SUCCESS ? NULL : "derivation failed");
}

/* NIST SP 800-38D AES-128-GCM test case (all-zero key/IV, 16-byte PT) */
static void test_aes_gcm(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char key[16] = { 0 };
    unsigned char nonce[12] = { 0 };
    unsigned char pt[16] = { 0 };
    unsigned char ct[32];
    unsigned char recovered[16];
    size_t olen = 0, plen = 0;
    static const unsigned char exp_ct[16] = {
        0x03, 0x88, 0xda, 0xce, 0x60, 0xb6, 0xa3, 0x92,
        0xf3, 0x28, 0xc2, 0xb9, 0x71, 0xb2, 0xfe, 0x78
    };
    static const unsigned char exp_tag[16] = {
        0xab, 0x6e, 0x47, 0xd4, 0x2c, 0xec, 0x13, 0xbd,
        0xf5, 0x3a, 0x67, 0xb2, 0x12, 0x57, 0xbd, 0xdf
    };
    psa_status_t st;

    st = import_aes_key(&kid, key, sizeof(key), PSA_ALG_GCM,
                        PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    if (st != PSA_SUCCESS) {
        report_psa(st, "AES-GCM import");
        return;
    }
    st = psa_aead_encrypt(kid, PSA_ALG_GCM, nonce, sizeof(nonce),
                          NULL, 0, pt, sizeof(pt), ct, sizeof(ct), &olen);
    report(st == PSA_SUCCESS && olen == 32 &&
           hexeq(ct, exp_ct, 16) && hexeq(ct + 16, exp_tag, 16),
           "AES-128-GCM encrypt KAT", NULL);
    st = psa_aead_decrypt(kid, PSA_ALG_GCM, nonce, sizeof(nonce),
                          NULL, 0, ct, olen, recovered, sizeof(recovered), &plen);
    report(st == PSA_SUCCESS && plen == 16 && hexeq(recovered, pt, 16),
           "AES-128-GCM decrypt round-trip", NULL);
    psa_destroy_key(kid);
}

static void test_chachapoly(void)
{
    mbedtls_svc_key_id_t kid = 0;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    unsigned char key[32], nonce[12], pt[32], ct[48], back[32];
    size_t olen = 0, plen = 0;
    psa_status_t st;

    memset(key, 0x11, sizeof(key));
    memset(nonce, 0x22, sizeof(nonce));
    memset(pt, 0x33, sizeof(pt));
    psa_set_key_type(&attr, PSA_KEY_TYPE_CHACHA20);
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_CHACHA20_POLY1305);
    st = psa_import_key(&attr, key, sizeof(key), &kid);
    if (st != PSA_SUCCESS) {
        report_psa(st, "ChaCha20-Poly1305 import");
        return;
    }
    st = psa_aead_encrypt(kid, PSA_ALG_CHACHA20_POLY1305, nonce, sizeof(nonce),
                          (const unsigned char *) "aad", 3,
                          pt, sizeof(pt), ct, sizeof(ct), &olen);
    if (st == PSA_SUCCESS) {
        st = psa_aead_decrypt(kid, PSA_ALG_CHACHA20_POLY1305, nonce, sizeof(nonce),
                              (const unsigned char *) "aad", 3,
                              ct, olen, back, sizeof(back), &plen);
    }
    report(st == PSA_SUCCESS && plen == sizeof(pt) && hexeq(back, pt, sizeof(pt)),
           "ChaCha20-Poly1305 round-trip", NULL);
    psa_destroy_key(kid);
}

static void test_aes_cbc(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char key[16], pt[32], ct[48], back[32];
    size_t olen = 0, plen = 0;
    psa_status_t st;

    memset(key, 0x44, sizeof(key));
    memset(pt, 0x66, sizeof(pt));
    st = import_aes_key(&kid, key, sizeof(key), PSA_ALG_CBC_NO_PADDING,
                        PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    if (st != PSA_SUCCESS) {
        report_psa(st, "AES-CBC import");
        return;
    }
    st = psa_cipher_encrypt(kid, PSA_ALG_CBC_NO_PADDING, pt, sizeof(pt),
                            ct, sizeof(ct), &olen);
    /* psa_cipher_encrypt generates a random IV prepended to ciphertext. */
    if (st == PSA_SUCCESS) {
        st = psa_cipher_decrypt(kid, PSA_ALG_CBC_NO_PADDING, ct, olen,
                                back, sizeof(back), &plen);
    }
    report(st == PSA_SUCCESS && plen == sizeof(pt) && hexeq(back, pt, sizeof(pt)),
           "AES-CBC-no-pad round-trip", NULL);
    psa_destroy_key(kid);
}

static void test_cmac(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char key[16], mac[16];
    size_t mac_len = 0;
    const unsigned char msg[] = "cmac-test-message";
    psa_status_t st;

    memset(key, 0x77, sizeof(key));
    st = import_aes_key(&kid, key, sizeof(key), PSA_ALG_CMAC,
                        PSA_KEY_USAGE_SIGN_MESSAGE | PSA_KEY_USAGE_VERIFY_MESSAGE);
    if (st != PSA_SUCCESS) {
        report_psa(st, "CMAC import");
        return;
    }
    st = psa_mac_compute(kid, PSA_ALG_CMAC, msg, sizeof(msg) - 1,
                         mac, sizeof(mac), &mac_len);
    if (st == PSA_SUCCESS) {
        st = psa_mac_verify(kid, PSA_ALG_CMAC, msg, sizeof(msg) - 1, mac, mac_len);
    }
    report_psa(st, "AES-CMAC compute/verify");
    psa_destroy_key(kid);
}

static void test_rsa(void)
{
    mbedtls_svc_key_id_t kid = 0;
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    unsigned char hash[32], sig[512], pt[32], ct[512], back[512];
    size_t sig_len = 0, ct_len = 0, pt_len = 0;
    psa_status_t st;

    memset(hash, 0xa5, sizeof(hash));
    memset(pt, 0x5a, sizeof(pt));
    psa_set_key_type(&attr, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attr, 2048);
    psa_set_key_usage_flags(&attr,
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                            PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256));
    /* One key cannot combine PKCS1v15-sign with OAEP encrypt in the policy.
     * Generate a signing key, then a separate wrapping key. */
    st = psa_generate_key(&attr, &kid);
    if (st != PSA_SUCCESS) {
        report_psa(st, "RSA-2048 generate (sign)");
        return;
    }
    st = psa_sign_hash(kid, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                       hash, sizeof(hash), sig, sizeof(sig), &sig_len);
    if (st == PSA_SUCCESS) {
        st = psa_verify_hash(kid, PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256),
                             hash, sizeof(hash), sig, sig_len);
    }
    report_psa(st, "RSA-PKCS1v1.5 SHA-256 sign/verify");
    psa_destroy_key(kid);
    kid = 0;

    attr = psa_key_attributes_init();
    psa_set_key_type(&attr, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attr, 2048);
    psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attr, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256));
    st = psa_generate_key(&attr, &kid);
    if (st != PSA_SUCCESS) {
        report_psa(st, "RSA-2048 generate (OAEP)");
        return;
    }
    st = psa_asymmetric_encrypt(kid, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
                                pt, sizeof(pt), NULL, 0,
                                ct, sizeof(ct), &ct_len);
    if (st == PSA_SUCCESS) {
        st = psa_asymmetric_decrypt(kid, PSA_ALG_RSA_OAEP(PSA_ALG_SHA_256),
                                    ct, ct_len, NULL, 0,
                                    back, sizeof(back), &pt_len);
    }
    report(st == PSA_SUCCESS && pt_len == sizeof(pt) && hexeq(back, pt, sizeof(pt)),
           "RSA-OAEP-SHA256 encrypt/decrypt", NULL);
    psa_destroy_key(kid);
}

static psa_status_t make_ec_key(mbedtls_svc_key_id_t *id,
                                psa_algorithm_t alg, psa_key_usage_t usage)
{
    psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attr, 256);
    psa_set_key_usage_flags(&attr, usage);
    psa_set_key_algorithm(&attr, alg);
    return psa_generate_key(&attr, id);
}

static void test_ecdsa(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char hash[32], sig[80];
    size_t sig_len = 0;
    psa_status_t st;

    memset(hash, 0x11, sizeof(hash));
    st = make_ec_key(&kid, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                     PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                     PSA_KEY_USAGE_EXPORT);
    if (st != PSA_SUCCESS) {
        report_psa(st, "ECDSA P-256 generate");
        return;
    }
    st = psa_sign_hash(kid, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                       hash, sizeof(hash), sig, sizeof(sig), &sig_len);
    if (st == PSA_SUCCESS) {
        st = psa_verify_hash(kid, PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                             hash, sizeof(hash), sig, sig_len);
    }
    report_psa(st, "ECDSA P-256 SHA-256 sign/verify");
    psa_destroy_key(kid);
}

static void test_ecdh(void)
{
    mbedtls_svc_key_id_t a = 0, b = 0;
    unsigned char pub_a[65], pub_b[65], secret_a[32], secret_b[32];
    size_t pub_a_len = 0, pub_b_len = 0, sec_a_len = 0, sec_b_len = 0;
    psa_status_t st;

    st = make_ec_key(&a, PSA_ALG_ECDH,
                     PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    if (st == PSA_SUCCESS) {
        st = make_ec_key(&b, PSA_ALG_ECDH,
                         PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
    }
    if (st == PSA_SUCCESS) {
        st = psa_export_public_key(a, pub_a, sizeof(pub_a), &pub_a_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_export_public_key(b, pub_b, sizeof(pub_b), &pub_b_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_raw_key_agreement(PSA_ALG_ECDH, a, pub_b, pub_b_len,
                                   secret_a, sizeof(secret_a), &sec_a_len);
    }
    if (st == PSA_SUCCESS) {
        st = psa_raw_key_agreement(PSA_ALG_ECDH, b, pub_a, pub_a_len,
                                   secret_b, sizeof(secret_b), &sec_b_len);
    }
    report(st == PSA_SUCCESS && sec_a_len == sec_b_len &&
           hexeq(secret_a, secret_b, sec_a_len),
           "ECDH P-256 key agreement", NULL);
    psa_destroy_key(a);
    psa_destroy_key(b);
}

static void test_random(void)
{
    unsigned char a[32], b[32];
    psa_status_t st1, st2;

    st1 = psa_generate_random(a, sizeof(a));
    st2 = psa_generate_random(b, sizeof(b));
    report(st1 == PSA_SUCCESS && st2 == PSA_SUCCESS && memcmp(a, b, sizeof(a)) != 0,
           "psa_generate_random uniqueness", NULL);
}

static void test_nist_kw(void)
{
    mbedtls_svc_key_id_t kid = 0;
    unsigned char kek[16], plain[16], wrapped[32], unwrapped[16];
    size_t wlen = 0, ulen = 0;
    psa_status_t st;

    memset(kek, 0x88, sizeof(kek));
    memset(plain, 0x99, sizeof(plain));
    st = import_aes_key(&kid, kek, sizeof(kek), PSA_ALG_ECB_NO_PADDING,
                        PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
    if (st != PSA_SUCCESS) {
        report_psa(st, "NIST-KW import KEK");
        return;
    }
    st = mbedtls_nist_kw_wrap(kid, MBEDTLS_KW_MODE_KW,
                              plain, sizeof(plain), wrapped, sizeof(wrapped), &wlen);
    if (st == PSA_SUCCESS) {
        st = mbedtls_nist_kw_unwrap(kid, MBEDTLS_KW_MODE_KW,
                                    wrapped, wlen, unwrapped, sizeof(unwrapped), &ulen);
    }
    report(st == PSA_SUCCESS && ulen == sizeof(plain) &&
           hexeq(unwrapped, plain, sizeof(plain)),
           "NIST SP 800-38F KW wrap/unwrap", NULL);
    psa_destroy_key(kid);
}

/* ---- X.509 + CSR ---- */
static int make_self_signed_ecdsa(mbedtls_pk_context *pk,
                                  mbedtls_x509_crt *crt,
                                  mbedtls_svc_key_id_t *psa_key)
{
    mbedtls_x509write_cert wrt;
    unsigned char der[2048];
    int len, ret;
    const unsigned char serial[] = { 0x01, 0x02, 0x03, 0x04 };

    *psa_key = 0;
    mbedtls_pk_init(pk);
    mbedtls_x509_crt_init(crt);
    mbedtls_x509write_crt_init(&wrt);

    ret = (int) make_ec_key(psa_key,
                            PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                            PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_VERIFY_HASH |
                            PSA_KEY_USAGE_EXPORT);
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_pk_copy_from_psa(*psa_key, pk);
    if (ret != 0) {
        goto fail;
    }

    mbedtls_x509write_crt_set_version(&wrt, MBEDTLS_X509_CRT_VERSION_3);
    mbedtls_x509write_crt_set_md_alg(&wrt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&wrt, pk);
    mbedtls_x509write_crt_set_issuer_key(&wrt, pk);
    ret = mbedtls_x509write_crt_set_subject_name(&wrt, "CN=mbedtls-test,O=Test,C=CN");
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_x509write_crt_set_issuer_name(&wrt, "CN=mbedtls-test,O=Test,C=CN");
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_x509write_crt_set_serial_raw(&wrt, serial, sizeof(serial));
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_x509write_crt_set_validity(&wrt, "20250101000000", "20351231235959");
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_x509write_crt_set_basic_constraints(&wrt, 1, -1);
    if (ret != 0) {
        goto fail;
    }
    ret = mbedtls_x509write_crt_set_key_usage(&wrt,
                                              MBEDTLS_X509_KU_DIGITAL_SIGNATURE |
                                              MBEDTLS_X509_KU_KEY_CERT_SIGN |
                                              MBEDTLS_X509_KU_KEY_AGREEMENT);
    if (ret != 0) {
        goto fail;
    }

    len = mbedtls_x509write_crt_der(&wrt, der, sizeof(der));
    if (len < 0) {
        ret = len;
        goto fail;
    }
    ret = mbedtls_x509_crt_parse_der(crt, der + sizeof(der) - len, (size_t) len);
    if (ret != 0) {
        goto fail;
    }

    mbedtls_x509write_crt_free(&wrt);
    return 0;

fail:
    mbedtls_x509write_crt_free(&wrt);
    mbedtls_x509_crt_free(crt);
    mbedtls_pk_free(pk);
    if (*psa_key != 0) {
        psa_destroy_key(*psa_key);
        *psa_key = 0;
    }
    return ret;
}

static void test_x509(void)
{
    mbedtls_pk_context pk;
    mbedtls_x509_crt crt;
    mbedtls_svc_key_id_t kid = 0;
    mbedtls_x509write_csr csr_wrt;
    mbedtls_x509_csr csr;
    unsigned char csr_der[1024];
    int ret, csr_len;
    uint32_t flags = 0;

    ret = make_self_signed_ecdsa(&pk, &crt, &kid);
    report_mbed(ret, "X.509 self-signed ECDSA P-256 write/parse");
    if (ret != 0) {
        return;
    }

    ret = mbedtls_x509_crt_verify(&crt, &crt, NULL, NULL, &flags, NULL, NULL);
    report(ret == 0 && flags == 0, "X.509 verify self-signed as trust anchor", NULL);

    mbedtls_x509write_csr_init(&csr_wrt);
    mbedtls_x509_csr_init(&csr);
    mbedtls_x509write_csr_set_md_alg(&csr_wrt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_csr_set_key(&csr_wrt, &pk);
    ret = mbedtls_x509write_csr_set_subject_name(&csr_wrt, "CN=mbedtls-csr");
    if (ret == 0) {
        csr_len = mbedtls_x509write_csr_der(&csr_wrt, csr_der, sizeof(csr_der));
        if (csr_len > 0) {
            ret = mbedtls_x509_csr_parse_der(&csr,
                                             csr_der + sizeof(csr_der) - csr_len,
                                             (size_t) csr_len);
        } else {
            ret = csr_len;
        }
    }
    report_mbed(ret, "X.509 CSR write/parse");

    mbedtls_x509write_csr_free(&csr_wrt);
    mbedtls_x509_csr_free(&csr);
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);
    psa_destroy_key(kid);
}

/* ---- in-memory TLS BIO ---- */
#define FIFO_CAP 65536

typedef struct {
    unsigned char buf[FIFO_CAP];
    size_t len;
} fifo;

typedef struct {
    fifo *in;
    fifo *out;
} bio_pair;

static int bio_send(void *ctx, const unsigned char *buf, size_t len)
{
    bio_pair *b = (bio_pair *) ctx;
    if (len > FIFO_CAP - b->out->len) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    memcpy(b->out->buf + b->out->len, buf, len);
    b->out->len += len;
    return (int) len;
}

static int bio_recv(void *ctx, unsigned char *buf, size_t len)
{
    bio_pair *b = (bio_pair *) ctx;
    if (b->in->len == 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if (len > b->in->len) {
        len = b->in->len;
    }
    memcpy(buf, b->in->buf, len);
    memmove(b->in->buf, b->in->buf + len, b->in->len - len);
    b->in->len -= len;
    return (int) len;
}

static int handshake_pump(mbedtls_ssl_context *cli, mbedtls_ssl_context *srv)
{
    int c = 1, s = 1;
    int i;

    for (i = 0; i < 2000 && (c != 0 || s != 0); i++) {
        if (c != 0) {
            c = mbedtls_ssl_handshake(cli);
            if (c != 0 && c != MBEDTLS_ERR_SSL_WANT_READ &&
                c != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return c;
            }
        }
        if (s != 0) {
            s = mbedtls_ssl_handshake(srv);
            if (s != 0 && s != MBEDTLS_ERR_SSL_WANT_READ &&
                s != MBEDTLS_ERR_SSL_WANT_WRITE) {
                return s;
            }
        }
    }
    if (c != 0 || s != 0) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
    return 0;
}

static int tls_exchange(mbedtls_ssl_protocol_version ver, const char *label)
{
    mbedtls_pk_context pk;
    mbedtls_x509_crt crt;
    mbedtls_svc_key_id_t kid = 0;
    mbedtls_ssl_config conf_c, conf_s;
    mbedtls_ssl_context ssl_c, ssl_s;
    fifo c2s, s2c;
    bio_pair bio_c, bio_s;
    unsigned char msg[] = "mbedtls-ping";
    unsigned char reply[64];
    int ret, n;
    char err[128];

    memset(&c2s, 0, sizeof(c2s));
    memset(&s2c, 0, sizeof(s2c));
    bio_c.in = &s2c;
    bio_c.out = &c2s;
    bio_s.in = &c2s;
    bio_s.out = &s2c;

    ret = make_self_signed_ecdsa(&pk, &crt, &kid);
    if (ret != 0) {
        snprintf(err, sizeof(err), "cert rc=%d", ret);
        report(0, label, err);
        return ret;
    }

    mbedtls_ssl_config_init(&conf_c);
    mbedtls_ssl_config_init(&conf_s);
    mbedtls_ssl_init(&ssl_c);
    mbedtls_ssl_init(&ssl_s);

    ret = mbedtls_ssl_config_defaults(&conf_c, MBEDTLS_SSL_IS_CLIENT,
                                      MBEDTLS_SSL_TRANSPORT_STREAM,
                                      MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret == 0) {
        ret = mbedtls_ssl_config_defaults(&conf_s, MBEDTLS_SSL_IS_SERVER,
                                          MBEDTLS_SSL_TRANSPORT_STREAM,
                                          MBEDTLS_SSL_PRESET_DEFAULT);
    }
    if (ret != 0) {
        goto done;
    }

    mbedtls_ssl_conf_min_tls_version(&conf_c, ver);
    mbedtls_ssl_conf_max_tls_version(&conf_c, ver);
    mbedtls_ssl_conf_min_tls_version(&conf_s, ver);
    mbedtls_ssl_conf_max_tls_version(&conf_s, ver);

    mbedtls_ssl_conf_authmode(&conf_c, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf_c, &crt, NULL);
    mbedtls_ssl_conf_authmode(&conf_s, MBEDTLS_SSL_VERIFY_NONE);
    ret = mbedtls_ssl_conf_own_cert(&conf_s, &crt, &pk);
    if (ret != 0) {
        goto done;
    }

    ret = mbedtls_ssl_setup(&ssl_c, &conf_c);
    if (ret == 0) {
        ret = mbedtls_ssl_setup(&ssl_s, &conf_s);
    }
    if (ret != 0) {
        goto done;
    }

    mbedtls_ssl_set_bio(&ssl_c, &bio_c, bio_send, bio_recv, NULL);
    mbedtls_ssl_set_bio(&ssl_s, &bio_s, bio_send, bio_recv, NULL);
    ret = mbedtls_ssl_set_hostname(&ssl_c, "mbedtls-test");
    if (ret != 0) {
        goto done;
    }

    ret = handshake_pump(&ssl_c, &ssl_s);
    if (ret != 0) {
        goto done;
    }

    n = mbedtls_ssl_write(&ssl_c, msg, sizeof(msg) - 1);
    if (n == (int) (sizeof(msg) - 1)) {
        n = mbedtls_ssl_read(&ssl_s, reply, sizeof(reply));
    } else if (n < 0) {
        ret = n;
        goto done;
    }
    if (n == (int) (sizeof(msg) - 1) && memcmp(reply, msg, (size_t) n) == 0) {
        ret = 0;
    } else if (n < 0) {
        ret = n;
    } else {
        ret = -1;
    }

done:
    if (ret == 0) {
        printf("        negotiated %s / %s\n",
               mbedtls_ssl_get_version(&ssl_c),
               mbedtls_ssl_get_ciphersuite(&ssl_c));
        report(1, label, NULL);
    } else {
        mbedtls_strerror(ret, err, sizeof(err));
        report(0, label, err);
    }

    mbedtls_ssl_free(&ssl_c);
    mbedtls_ssl_free(&ssl_s);
    mbedtls_ssl_config_free(&conf_c);
    mbedtls_ssl_config_free(&conf_s);
    mbedtls_x509_crt_free(&crt);
    mbedtls_pk_free(&pk);
    if (kid != 0) {
        psa_destroy_key(kid);
    }
    return ret;
}

static void test_version(void)
{
    const char *s = mbedtls_version_get_string();
    unsigned int n = mbedtls_version_get_number();
    report(s != NULL && strstr(s, "4.2.0") != NULL && n == 0x04020000,
           "mbedtls_version 4.2.0", s);
}

int main(void)
{
    psa_status_t st;

    printf("Mbed TLS 4.2 functional test (Debian 13 / C99+POSIX default config)\n");
    printf("compile-time: %s\n", MBEDTLS_VERSION_STRING_FULL);

    st = psa_crypto_init();
    if (st != PSA_SUCCESS) {
        printf("psa_crypto_init failed: %d\n", (int) st);
        return 1;
    }

    test_version();
    test_hash();
    test_hmac();
    test_hkdf();
    test_aes_gcm();
    test_chachapoly();
    test_aes_cbc();
    test_cmac();
    test_random();
    test_nist_kw();
    test_ecdsa();
    test_ecdh();
    test_rsa();
    test_x509();
    tls_exchange(MBEDTLS_SSL_VERSION_TLS1_2, "TLS 1.2 ECDHE-ECDSA memory-BIO");
    tls_exchange(MBEDTLS_SSL_VERSION_TLS1_3, "TLS 1.3 ECDHE-ECDSA memory-BIO");

    mbedtls_psa_crypto_free();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
