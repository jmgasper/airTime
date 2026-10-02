/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_LANGUAGES_H
#define AIRTIME_LANGUAGES_H


#include <String.h>


namespace airtime {

// The English name of an ISO 639-1 or 639-2 (B or T) code; an empty string
// for "und", "zxx" and codes it does not know.
BString language_name(const char* code);

// The two letter code for a language code if there is one, otherwise the
// code itself in lower case. "fre", "fra" and "fr" all become "fr".
BString canonical_language(const char* code);

// True if two codes name the same language.
bool same_language(const char* a, const char* b);

}	// namespace airtime

#endif	// AIRTIME_LANGUAGES_H
