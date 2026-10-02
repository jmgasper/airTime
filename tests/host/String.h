// Enough of Haiku's BString, on std::string, for the engine's tests.
#ifndef HOST_STRING_H
#define HOST_STRING_H
#include <algorithm>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>
#include <strings.h>
#include "SupportDefs.h"

class BString {
public:
	BString() {}
	BString(const char* text) : fText(text != NULL ? text : "") {}
	BString(const char* text, int32 length) : fText(text, length) {}
	BString(const BString& other) = default;
	BString& operator=(const BString& other) = default;
	BString& operator=(const char* text) { fText = text != NULL ? text : ""; return *this; }

	const char* String() const { return fText.c_str(); }
	int32 Length() const { return (int32)fText.size(); }
	char operator[](int32 index) const { return fText[index]; }

	BString& SetTo(const char* text) { fText = text != NULL ? text : ""; return *this; }
	BString& SetTo(const char* text, int32 length) { fText.assign(text, length); return *this; }
	BString& SetToFormat(const char* format, ...)
	{
		char buffer[4096];
		va_list arguments;
		va_start(arguments, format);
		vsnprintf(buffer, sizeof(buffer), format, arguments);
		va_end(arguments);
		fText = buffer;
		return *this;
	}
	BString& Append(char c, int32 count) { fText.append(count, c); return *this; }
	BString& operator<<(const char* text) { fText += text; return *this; }
	BString& operator<<(const BString& text) { fText += text.fText; return *this; }
	BString& operator<<(int32 value) { fText += std::to_string(value); return *this; }
	BString& operator<<(char c) { fText += c; return *this; }

	int32 FindFirst(char c, int32 from = 0) const
	{ size_t at = fText.find(c, from); return at == std::string::npos ? -1 : (int32)at; }
	int32 FindFirst(const char* text) const
	{ size_t at = fText.find(text); return at == std::string::npos ? -1 : (int32)at; }
	int32 FindLast(char c) const
	{ size_t at = fText.rfind(c); return at == std::string::npos ? -1 : (int32)at; }
	int32 IFindFirst(const char* text) const
	{
		std::string a = fText, b = text;
		std::transform(a.begin(), a.end(), a.begin(), ::tolower);
		std::transform(b.begin(), b.end(), b.begin(), ::tolower);
		size_t at = a.find(b);
		return at == std::string::npos ? -1 : (int32)at;
	}
	BString& CopyInto(BString& into, int32 from, int32 length) const
	{ into.fText = fText.substr(from, length); return into; }
	BString& Truncate(int32 length) { if (length < Length()) fText.resize(length); return *this; }
	BString& Remove(int32 from, int32 length) { fText.erase(from, length); return *this; }
	BString& ToLower() { std::transform(fText.begin(), fText.end(), fText.begin(), ::tolower); return *this; }
	BString& ToUpper() { std::transform(fText.begin(), fText.end(), fText.begin(), ::toupper); return *this; }
	BString& Trim()
	{
		size_t start = fText.find_first_not_of(" \t\r\n");
		size_t end = fText.find_last_not_of(" \t\r\n");
		fText = start == std::string::npos ? "" : fText.substr(start, end - start + 1);
		return *this;
	}
	bool StartsWith(const char* text) const { return fText.rfind(text, 0) == 0; }
	bool StartsWith(const BString& text) const { return StartsWith(text.String()); }
	bool EndsWith(const char* text) const
	{
		std::string end(text);
		return fText.size() >= end.size()
			&& fText.compare(fText.size() - end.size(), end.size(), end) == 0;
	}
	int Compare(const BString& other) const { return fText.compare(other.fText); }
	int ICompare(const BString& other) const { return strcasecmp(String(), other.String()); }
	int ICompare(const char* other) const { return strcasecmp(String(), other); }
	bool operator==(const BString& other) const { return fText == other.fText; }
	bool operator==(const char* other) const { return fText == other; }
	bool operator!=(const BString& other) const { return fText != other.fText; }
	bool operator!=(const char* other) const { return fText != other; }

private:
	std::string fText;
};
#endif
