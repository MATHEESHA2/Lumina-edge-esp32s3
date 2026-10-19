#pragma once
#include <Arduino.h>

bool jsonFindValue(const String& j, const char* key, int& start);
bool jsonGetString(const String& j, const char* key, String& out);
bool jsonGetBool(const String& j, const char* key, bool& out);
bool jsonGetNumber(const String& j, const char* key, long& out);
