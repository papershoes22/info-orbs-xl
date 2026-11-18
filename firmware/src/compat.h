/* Just enough of Arduino for the orbs' original drawing code to compile unchanged on the XL and on the PC
   (2026-11-16): its String, millis(), byte, the two colour names it uses. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>

typedef uint8_t byte;
#ifndef TFT_BLACK
#define TFT_BLACK 0x0000
#define TFT_WHITE 0xFFFF
#endif

extern "C" uint32_t orbs_millis(void);     /* main.cpp on the XL, the preview's clock on the PC */
static inline uint32_t millis(void) { return orbs_millis(); }
extern "C" uint32_t orbs_random32(void);    /* the RP2350's random source on the XL, rand() on the PC */
static inline long random(long lo, long hi) { return hi <= lo ? lo : lo + (long)(orbs_random32() % (uint32_t)(hi - lo)); }
static inline long map(long x, long in_min, long in_max, long out_min, long out_max)
{
	return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

class String {
	std::string s;

public:
	String() {}
	String(const char *c) : s(c ? c : "") {}
	String(const std::string &x) : s(x) {}
	String(char c) : s(1, c) {}
	String(int v) : s(std::to_string(v)) {}
	String(unsigned v) : s(std::to_string(v)) {}
	String(long v) : s(std::to_string(v)) {}
	String(unsigned long v) : s(std::to_string(v)) {}
	String(float v, int dec = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, (double)v); s = b; }
	String(double v, int dec = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", dec, v); s = b; }
	unsigned length() const { return (unsigned)s.size(); }
	char charAt(unsigned i) const { return i < s.size() ? s[i] : 0; }
	char operator[](unsigned i) const { return charAt(i); }
	const char *c_str() const { return s.c_str(); }
	String substring(unsigned a) const { return a < s.size() ? String(s.substr(a)) : String(); }
	String substring(unsigned a, unsigned b) const
	{
		if(a >= s.size() || b <= a)
			return String();
		return String(s.substr(a, b - a));
	}
	long toInt() const { return strtol(s.c_str(), nullptr, 10); }
	float toFloat() const { return strtof(s.c_str(), nullptr); }
	int indexOf(char c, unsigned from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
	int indexOf(const char *t, unsigned from = 0) const { size_t p = s.find(t, from); return p == std::string::npos ? -1 : (int)p; }
	bool startsWith(const char *t) const { return s.rfind(t, 0) == 0; }
	bool isEmpty() const { return s.empty(); }
	void toUpperCase() { for(auto &c : s) c = (char)toupper((unsigned char)c); }
	void toLowerCase() { for(auto &c : s) c = (char)tolower((unsigned char)c); }
	void remove(unsigned idx) { if(idx < s.size()) s.erase(idx); }
	void remove(unsigned idx, unsigned n) { if(idx < s.size()) s.erase(idx, n); }
	void replace(const char *a, const char *b)
	{
		std::string from(a), to(b);
		if(from.empty())
			return;
		for(size_t p = 0; (p = s.find(from, p)) != std::string::npos; p += to.size())
			s.replace(p, from.size(), to);
	}
	void trim()
	{
		size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
		s = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
	}
	String &operator+=(const String &o) { s += o.s; return *this; }
	String &operator+=(const char *o) { s += o; return *this; }
	String &operator+=(char c) { s += c; return *this; }
	friend String operator+(const String &a, const String &b) { return String(a.s + b.s); }
	friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.s); }
	friend String operator+(const String &a, const char *b) { return String(a.s + b); }
	bool operator==(const String &o) const { return s == o.s; }
	bool operator==(const char *o) const { return s == o; }
	bool operator!=(const String &o) const { return s != o.s; }
	bool operator!=(const char *o) const { return s != o; }
};
