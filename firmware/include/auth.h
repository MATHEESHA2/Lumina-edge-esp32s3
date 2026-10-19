#pragma once
#include <Arduino.h>

namespace auth {

struct LoginResult {
  int    httpCode;
  String error;
  String token;
};

void begin(const char* adminPassword);
String issueNonce();
LoginResult login(const String& nonce, const String& proof);
bool tokenValid(const String& bearerHeader);

}
