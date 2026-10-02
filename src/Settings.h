/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_SETTINGS_H
#define AIRTIME_SETTINGS_H


#include <Rect.h>
#include <String.h>


namespace airtime {

struct Settings {
	float		volume;
	bool		muted;
	float		subtitleScale;
	bool		loop;
	bool		hardwareDecoding;
	bool		snapToAspect;
	bool		autoPlay;
	int32		timeDisplay;
	BString		audioLanguage;		// "" = the file's default
	BString		subtitleLanguage;	// "" = off unless forced
	BRect		inspectorFrame;

				Settings();
	void		Load();
	void		Save() const;
};

Settings& settings();

}	// namespace airtime

#endif	// AIRTIME_SETTINGS_H
