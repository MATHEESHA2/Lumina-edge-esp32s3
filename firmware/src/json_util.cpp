#include "json_util.h"
#include <math.h>

bool jsonFindValue(const String& j, const char* key, int& start) {
  String pat = String("\"") + key + "\"";
  int i = j.indexOf(pat);
  if (i < 0) return false;
  i += pat.length();
  while (i < (int)j.length() && isspace((unsigned char)j[i])) i++;
  if (i >= (int)j.length() || j[i] != ':') return false;
  i++;
  while (i < (int)j.length() && isspace((unsigned char)j[i])) i++;
  if (i >= (int)j.length()) return false;
  start = i;
  return true;
}

bool jsonGetString(const String& j, const char* key, String& out) {
  int s;
  if (!jsonFindValue(j, key, s) || j[s] != '"') return false;
  int e = j.indexOf('"', s + 1);
  if (e < 0) return false;
  out = j.substring(s + 1, e);
  return true;
}

bool jsonGetBool(const String& j, const char* key, bool& out) {
  int s;
  if (!jsonFindValue(j, key, s)) return false;
  if (j.startsWith("true", s))  { out = true;  return true; }
  if (j.startsWith("false", s)) { out = false; return true; }
  return false;
}

bool jsonGetNumber(const String& j, const char* key, long& out) {
  int s;
  if (!jsonFindValue(j, key, s)) return false;
  int e = s;
  if (e < (int)j.length() && j[e] == '-') e++;
  int d0 = e;
  while (e < (int)j.length() && (isDigit(j[e]) || j[e] == '.')) e++;
  if (e == d0) return false;
  out = lroundf(j.substring(s, e).toFloat());
  return true;
}
