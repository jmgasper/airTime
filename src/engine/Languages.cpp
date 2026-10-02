/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Languages.h"

#include <ctype.h>
#include <string.h>


namespace airtime {

namespace {

struct Language {
	const char*	twoLetter;
	const char*	bibliographic;
	const char*	terminology;
	const char*	name;
};

// ISO 639-1 and 639-2. Only the languages film and television actually come
// in; anything else shows its code.
const Language kLanguages[] = {
	{"af", "afr", "afr", "Afrikaans"},
	{"sq", "alb", "sqi", "Albanian"},
	{"am", "amh", "amh", "Amharic"},
	{"ar", "ara", "ara", "Arabic"},
	{"hy", "arm", "hye", "Armenian"},
	{"as", "asm", "asm", "Assamese"},
	{"az", "aze", "aze", "Azerbaijani"},
	{"eu", "baq", "eus", "Basque"},
	{"be", "bel", "bel", "Belarusian"},
	{"bn", "ben", "ben", "Bengali"},
	{"bs", "bos", "bos", "Bosnian"},
	{"br", "bre", "bre", "Breton"},
	{"bg", "bul", "bul", "Bulgarian"},
	{"my", "bur", "mya", "Burmese"},
	{"ca", "cat", "cat", "Catalan"},
	{NULL, "yue", "yue", "Cantonese"},
	{"zh", "chi", "zho", "Chinese"},
	{NULL, "cmn", "cmn", "Mandarin"},
	{"hr", "hrv", "hrv", "Croatian"},
	{"cs", "cze", "ces", "Czech"},
	{"da", "dan", "dan", "Danish"},
	{"nl", "dut", "nld", "Dutch"},
	{"dz", "dzo", "dzo", "Dzongkha"},
	{"en", "eng", "eng", "English"},
	{"eo", "epo", "epo", "Esperanto"},
	{"et", "est", "est", "Estonian"},
	{"fo", "fao", "fao", "Faroese"},
	{NULL, "fil", "fil", "Filipino"},
	{"fi", "fin", "fin", "Finnish"},
	{"fr", "fre", "fra", "French"},
	{"fy", "fry", "fry", "Western Frisian"},
	{"gl", "glg", "glg", "Galician"},
	{"ka", "geo", "kat", "Georgian"},
	{"de", "ger", "deu", "German"},
	{"el", "gre", "ell", "Greek"},
	{"gn", "grn", "grn", "Guarani"},
	{"gu", "guj", "guj", "Gujarati"},
	{"ht", "hat", "hat", "Haitian Creole"},
	{"ha", "hau", "hau", "Hausa"},
	{"he", "heb", "heb", "Hebrew"},
	{"hi", "hin", "hin", "Hindi"},
	{"hu", "hun", "hun", "Hungarian"},
	{"is", "ice", "isl", "Icelandic"},
	{"ig", "ibo", "ibo", "Igbo"},
	{"id", "ind", "ind", "Indonesian"},
	{"ia", "ina", "ina", "Interlingua"},
	{"iu", "iku", "iku", "Inuktitut"},
	{"ga", "gle", "gle", "Irish"},
	{"it", "ita", "ita", "Italian"},
	{"ja", "jpn", "jpn", "Japanese"},
	{"jv", "jav", "jav", "Javanese"},
	{"kn", "kan", "kan", "Kannada"},
	{"kk", "kaz", "kaz", "Kazakh"},
	{"km", "khm", "khm", "Khmer"},
	{"rw", "kin", "kin", "Kinyarwanda"},
	{"ky", "kir", "kir", "Kyrgyz"},
	{"ko", "kor", "kor", "Korean"},
	{"ku", "kur", "kur", "Kurdish"},
	{"lo", "lao", "lao", "Lao"},
	{"la", "lat", "lat", "Latin"},
	{"lv", "lav", "lav", "Latvian"},
	{"lt", "lit", "lit", "Lithuanian"},
	{"lb", "ltz", "ltz", "Luxembourgish"},
	{"mk", "mac", "mkd", "Macedonian"},
	{"mg", "mlg", "mlg", "Malagasy"},
	{"ms", "may", "msa", "Malay"},
	{"ml", "mal", "mal", "Malayalam"},
	{"mt", "mlt", "mlt", "Maltese"},
	{"mi", "mao", "mri", "Maori"},
	{"mr", "mar", "mar", "Marathi"},
	{"mn", "mon", "mon", "Mongolian"},
	{"ne", "nep", "nep", "Nepali"},
	{"no", "nor", "nor", "Norwegian"},
	{"nb", "nob", "nob", "Norwegian Bokmål"},
	{"nn", "nno", "nno", "Norwegian Nynorsk"},
	{"oc", "oci", "oci", "Occitan"},
	{"or", "ori", "ori", "Odia"},
	{"ps", "pus", "pus", "Pashto"},
	{"fa", "per", "fas", "Persian"},
	{"pl", "pol", "pol", "Polish"},
	{"pt", "por", "por", "Portuguese"},
	{"pa", "pan", "pan", "Punjabi"},
	{"qu", "que", "que", "Quechua"},
	{"ro", "rum", "ron", "Romanian"},
	{"rm", "roh", "roh", "Romansh"},
	{"ru", "rus", "rus", "Russian"},
	{"sm", "smo", "smo", "Samoan"},
	{"gd", "gla", "gla", "Scottish Gaelic"},
	{"sr", "srp", "srp", "Serbian"},
	{"sn", "sna", "sna", "Shona"},
	{"sd", "snd", "snd", "Sindhi"},
	{"si", "sin", "sin", "Sinhala"},
	{"sk", "slo", "slk", "Slovak"},
	{"sl", "slv", "slv", "Slovenian"},
	{"so", "som", "som", "Somali"},
	{"es", "spa", "spa", "Spanish"},
	{"su", "sun", "sun", "Sundanese"},
	{"sw", "swa", "swa", "Swahili"},
	{"sv", "swe", "swe", "Swedish"},
	{"tl", "tgl", "tgl", "Tagalog"},
	{"tg", "tgk", "tgk", "Tajik"},
	{"ta", "tam", "tam", "Tamil"},
	{"tt", "tat", "tat", "Tatar"},
	{"te", "tel", "tel", "Telugu"},
	{"th", "tha", "tha", "Thai"},
	{"bo", "tib", "bod", "Tibetan"},
	{"ti", "tir", "tir", "Tigrinya"},
	{"to", "ton", "ton", "Tongan"},
	{"tr", "tur", "tur", "Turkish"},
	{"tk", "tuk", "tuk", "Turkmen"},
	{"ug", "uig", "uig", "Uyghur"},
	{"uk", "ukr", "ukr", "Ukrainian"},
	{"ur", "urd", "urd", "Urdu"},
	{"uz", "uzb", "uzb", "Uzbek"},
	{"vi", "vie", "vie", "Vietnamese"},
	{"cy", "wel", "cym", "Welsh"},
	{"wo", "wol", "wol", "Wolof"},
	{"xh", "xho", "xho", "Xhosa"},
	{"yi", "yid", "yid", "Yiddish"},
	{"yo", "yor", "yor", "Yoruba"},
	{"zu", "zul", "zul", "Zulu"},
};


const Language*
find_language(const char* code)
{
	if (code == NULL || code[0] == '\0')
		return NULL;

	// "en-US", "pt_BR": the region does not change the language.
	char lower[8];
	size_t length = 0;
	while (code[length] != '\0' && code[length] != '-' && code[length] != '_'
		&& length < sizeof(lower) - 1) {
		lower[length] = tolower((unsigned char)code[length]);
		length++;
	}
	lower[length] = '\0';

	for (const Language& language : kLanguages) {
		if ((language.twoLetter != NULL
				&& strcmp(language.twoLetter, lower) == 0)
			|| strcmp(language.bibliographic, lower) == 0
			|| strcmp(language.terminology, lower) == 0) {
			return &language;
		}
	}
	return NULL;
}

}	// namespace


BString
language_name(const char* code)
{
	const Language* language = find_language(code);
	if (language != NULL)
		return language->name;
	if (code == NULL || strcasecmp(code, "und") == 0
		|| strcasecmp(code, "zxx") == 0 || strcasecmp(code, "mis") == 0
		|| strcasecmp(code, "mul") == 0) {
		return "";
	}
	BString unknown(code);
	unknown.ToUpper();
	return unknown;
}


BString
canonical_language(const char* code)
{
	const Language* language = find_language(code);
	if (language != NULL) {
		return language->twoLetter != NULL
			? language->twoLetter : language->terminology;
	}
	BString lower(code != NULL ? code : "");
	lower.ToLower();
	return lower;
}


bool
same_language(const char* a, const char* b)
{
	if (a == NULL || b == NULL || a[0] == '\0' || b[0] == '\0')
		return false;
	return canonical_language(a) == canonical_language(b);
}

}	// namespace airtime
