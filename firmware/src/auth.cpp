#include "auth.h"
#include "settings.h"
#include "mbedtls/sha256.h"
#include "esp_system.h"

namespace {

struct Slot {
  char     v[33];
  uint32_t t;
  bool     used;
};

Slot     nonces[MAX_NONCES];
Slot     tokens[MAX_TOKENS];
String   adminHash;
int      failCount = 0;
bool     locked = false;
uint32_t lockUntil = 0;

String sha256Hex(const String& in) {
  uint8_t h[32];
  mbedtls_sha256((const unsigned char*)in.c_str(), in.length(), h, 0);
  static const char* hx = "0123456789abcdef";
  String o;
  o.reserve(64);
  for (int i = 0; i < 32; i++) {
    o += hx[h[i] >> 4];
    o += hx[h[i] & 15];
  }
  return o;
}

void randomHex(char* out, int bytes) {
  for (int i = 0; i < bytes; i++) sprintf(out + i * 2, "%02x", (unsigned)(esp_random() & 0xFF));
  out[bytes * 2] = 0;
}

bool safeEq(const String& a, const String& b) {
  if (a.length() != b.length()) return false;
  uint8_t d = 0;
  for (size_t i = 0; i < a.length(); i++) d |= (uint8_t)(a[i] ^ b[i]);
  return d == 0;
}

Slot* allocSlot(Slot* a, int n, uint32_t ttl) {
  uint32_t now = millis();
  Slot* oldest = &a[0];
  for (int i = 0; i < n; i++) {
    if (!a[i].used || now - a[i].t > ttl) return &a[i];
    if (now - a[i].t > now - oldest->t) oldest = &a[i];
  }
  return oldest;
}

}

namespace auth {

void begin(const char* adminPassword) {
  adminHash = sha256Hex(String(adminPassword));
}

String issueNonce() {
  Slot* n = allocSlot(nonces, MAX_NONCES, NONCE_TTL_MS);
  randomHex(n->v, 16);
  n->t = millis();
  n->used = true;
  return String(n->v);
}

LoginResult login(const String& nonce, const String& proof) {
  uint32_t now = millis();
  if (locked && (int32_t)(lockUntil - now) > 0) return {429, "Too many attempts, wait 30 s", ""};
  locked = false;

  Slot* n = nullptr;
  for (int i = 0; i < MAX_NONCES; i++)
    if (nonces[i].used && now - nonces[i].t <= NONCE_TTL_MS && nonce == String(nonces[i].v)) n = &nonces[i];
  if (!n) return {401, "Challenge expired, try again", ""};
  n->used = false;

  String p = proof;
  p.toLowerCase();
  if (!safeEq(sha256Hex(adminHash + nonce), p)) {
    if (++failCount >= MAX_FAILS) {
      failCount = 0;
      locked = true;
      lockUntil = now + LOCKOUT_MS;
      Serial.println("[AUTH] locked");
    }
    return {401, "Wrong password", ""};
  }

  failCount = 0;
  Slot* t = allocSlot(tokens, MAX_TOKENS, TOKEN_TTL_MS);
  randomHex(t->v, 16);
  t->t = now;
  t->used = true;
  return {200, "", String(t->v)};
}

bool tokenValid(const String& bearerHeader) {
  if (!bearerHeader.startsWith("Bearer ")) return false;
  String t = bearerHeader.substring(7);
  t.trim();
  uint32_t now = millis();
  bool ok = false;
  for (int i = 0; i < MAX_TOKENS; i++)
    if (tokens[i].used && now - tokens[i].t <= TOKEN_TTL_MS && safeEq(t, String(tokens[i].v))) ok = true;
  return ok;
}

}
