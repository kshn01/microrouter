#pragma once
#include <Arduino.h>
#include <mbedtls/sha256.h>
#include <mbedtls/pk.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/base64.h>
#include <mbedtls/aes.h>

// ─── Router RSA public key for ZTE Gateway ───
static const char ROUTER_RSA_PUBKEY[] =
  "-----BEGIN PUBLIC KEY-----\n"
  "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAodPTerkUVCYmv28SOfRV\n"
  "7UKHVujx/HjCUTAWy9l0L5H0JV0LfDudTdMNPEKloZsNam3YrtEnq6jqMLJV4ASb\n"
  "1d6axmIgJ636wyTUS99gj4BKs6bQSTUSE8h/QkUYv4gEIt3saMS0pZpd90y6+B/9\n"
  "hZxZE/RKU8e+zgRqp1/762TB7vcjtjOwXRDEL0w71Jk9i8VUQ59MR1Uj5E8X3WIc\n"
  "fYSK5RWBkMhfaTRM6ozS9Bqhi40xlSOb3GBxCmliCifOJNLoO9kFoWgAIw5hkSIb\n"
  "GH+4Csop9Uy8VvmmB+B3ubFLN35qIa5OG5+SDXn4L7FeAA5lRiGxRi8tsWrtew8w\n"
  "nwIDAQAB\n"
  "-----END PUBLIC KEY-----\n";

// XOR Mask Key for NVS Storage Obfuscation
static const char NVS_XOR_KEY[] = "Ant1grav1tySecKey!";

namespace CryptoHelpers {

// ─── XOR Obfuscation (Masking/Scrambling) Helpers ───
inline String xorObfuscate(const String& input) {
  if (input.length() == 0) return "";
  String obfuscated = "";
  size_t keyLen = strlen(NVS_XOR_KEY);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i] ^ NVS_XOR_KEY[i % keyLen];
    obfuscated += c;
  }
  
  size_t olen = 0;
  size_t b64len = (obfuscated.length() * 4 / 3) + 4;
  uint8_t* b64 = (uint8_t*)malloc(b64len);
  if (!b64) return "";
  mbedtls_base64_encode(b64, b64len, &olen, (const uint8_t*)obfuscated.c_str(), obfuscated.length());
  b64[olen] = '\0';
  String res = String((char*)b64);
  free(b64);
  return res;
}

inline String xorDeobfuscate(const String& base64Input) {
  if (base64Input.length() == 0) return "";
  size_t olen = 0;
  size_t declen = (base64Input.length() * 3 / 4) + 4;
  uint8_t* dec = (uint8_t*)malloc(declen);
  if (!dec) return "";
  int ret = mbedtls_base64_decode(dec, declen, &olen, (const uint8_t*)base64Input.c_str(), base64Input.length());
  if (ret != 0) {
    free(dec);
    return "";
  }
  
  String original = "";
  size_t keyLen = strlen(NVS_XOR_KEY);
  for (size_t i = 0; i < olen; i++) {
    char c = dec[i] ^ NVS_XOR_KEY[i % keyLen];
    original += c;
  }
  free(dec);
  return original;
}

// ─── JSON String Escaping Helper ───
inline String escapeJsonString(const String& str) {
  String escaped = "";
  escaped.reserve((size_t)(str.length() * 1.1f));
  for (size_t i = 0; i < str.length(); i++) {
    char c = str[i];
    if (c == '"') {
      escaped += "\\\"";
    } else if (c == '\\') {
      escaped += "\\\\";
    } else if (c == '\n') {
      escaped += "\\n";
    } else if (c == '\r') {
      escaped += "\\r";
    } else if (c == '\t') {
      escaped += "\\t";
    } else if ((unsigned char)c < 32) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\u%04x", c);
      escaped += buf;
    } else {
      escaped += c;
    }
  }
  return escaped;
}

// ─── Hex Escape Decoder (\xXX) ───
inline String decodeHexEscapes(const String& raw) {
  String decoded = "";
  decoded.reserve(raw.length());
  for (int i = 0; i < (int)raw.length(); ) {
    if (raw[i] == '\\' && i + 3 < (int)raw.length() && (raw[i+1] == 'x' || raw[i+1] == 'X')) {
      char hex[3] = {raw[i+2], raw[i+3], '\0'};
      decoded += (char)strtol(hex, nullptr, 16);
      i += 4;
    } else {
      decoded += raw[i++];
    }
  }
  return decoded;
}

// ─── Extract token from static HTML string ───
inline String extractTokenFromHtml(const String& html) {
  int idx = html.indexOf("_sessionTmpToken");
  if (idx < 0) return "";
  int q1 = html.indexOf("\"", idx);
  if (q1 < 0) return "";
  q1 += 1;
  int q2 = html.indexOf("\"", q1);
  if (q2 < 0) return "";
  return decodeHexEscapes(html.substring(q1, q2));
}

// ─── SHA256 Helper ───
inline void sha256Bytes(const String& input, uint8_t output[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const uint8_t*)input.c_str(), input.length());
  mbedtls_sha256_finish(&ctx, output);
  mbedtls_sha256_free(&ctx);
}

inline String sha256Hex(const String& input) {
  uint8_t hash[32];
  sha256Bytes(input, hash);
  String result = "";
  for (int i = 0; i < 32; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02x", hash[i]);
    result += buf;
  }
  return result;
}

// ─── AES-256-CBC ZeroPadding Encrypt -> Base64 ───
inline String aesCbcEncB64(const uint8_t key[32], const uint8_t iv[16], const String& plain) {
  size_t plen  = plain.length();
  size_t padLen = 16 - (plen % 16);
  if (padLen == 16) padLen = 0;
  size_t padded = plen + padLen;
  uint8_t* buf = (uint8_t*)malloc(padded);
  if (!buf) return "";
  memcpy(buf, plain.c_str(), plen);
  for (size_t i = plen; i < padded; i++) {
    buf[i] = 0; // ZeroPadding
  }

  uint8_t* cipher = (uint8_t*)malloc(padded);
  if (!cipher) { free(buf); return ""; }
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);

  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 256);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, padded, ivCopy, buf, cipher);
  mbedtls_aes_free(&aes);
  free(buf);

  size_t b64Alloc = padded * 2 + 4;
  uint8_t* b64 = (uint8_t*)malloc(b64Alloc);
  if (!b64) { free(cipher); return ""; }
  size_t b64len = 0;
  mbedtls_base64_encode(b64, b64Alloc, &b64len, cipher, padded);
  b64[b64len] = '\0';
  free(cipher);
  String res = String((char*)b64);
  free(b64);
  return res;
}

// ─── Generic URL Encode ───
inline String urlEncode(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += c;
    else if (c == ' ') out += "+";
    else {
      char hex[4];
      snprintf(hex, sizeof(hex), "%%%02X", (unsigned char)c);
      out += hex;
    }
  }
  return out;
}

// ─── URL-encode Base64 string for form POST body ───
inline String urlEncB64(const String& s) {
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if      (c == '+') out += "%2B";
    else if (c == '/') out += "%2F";
    else if (c == '=') out += "%3D";
    else               out += c;
  }
  return out;
}

// ─── RSA Public Key Encrypt (PKCS#1 v1.5) -> Base64 ───
inline String rsaEncryptBase64(const String& plaintext) {
  mbedtls_pk_context pk;
  mbedtls_pk_init(&pk);

  mbedtls_entropy_context  entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  const char* pers = "rsa_esp32_microrouter";
  int ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
                                 (const unsigned char*)pers, strlen(pers));
  if (ret != 0) {
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return "";
  }

  ret = mbedtls_pk_parse_public_key(&pk,
          (const unsigned char*)ROUTER_RSA_PUBKEY,
          strlen(ROUTER_RSA_PUBKEY) + 1);
  if (ret != 0) {
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return "";
  }

  uint8_t encrypted[256];
  size_t  olen = 0;
  ret = mbedtls_pk_encrypt(&pk,
          (const uint8_t*)plaintext.c_str(), plaintext.length(),
          encrypted, &olen, sizeof(encrypted),
          mbedtls_ctr_drbg_random, &ctr_drbg);
  if (ret != 0) {
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    return "";
  }

  uint8_t b64[400];
  size_t  b64len = 0;
  mbedtls_base64_encode(b64, sizeof(b64), &b64len, encrypted, olen);
  b64[b64len] = '\0';

  mbedtls_pk_free(&pk);
  mbedtls_entropy_free(&entropy);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  return String((char*)b64);
}

} // namespace CryptoHelpers
