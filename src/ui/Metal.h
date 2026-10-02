/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_METAL_H
#define AIRTIME_METAL_H


#include <GraphicsDefs.h>
#include <Rect.h>


class BBitmap;
class BView;


namespace airtime {

/*!	The look of the QuickTime Player of Mac OS 9 and the first Mac OS X:
	brushed aluminium, a recessed khaki LCD, round metal buttons. Everything
	is drawn here so the window and the full screen controller agree. */

enum button_state {
	BUTTON_NORMAL,
	BUTTON_HOVER,
	BUTTON_PRESSED,
	BUTTON_DISABLED
};

enum glyph_kind {
	GLYPH_PLAY,
	GLYPH_PAUSE,
	GLYPH_REWIND,
	GLYPH_FAST_FORWARD,
	GLYPH_TO_START,
	GLYPH_TO_END,
	GLYPH_FULL_SCREEN,
	GLYPH_EXIT_FULL_SCREEN,
	GLYPH_SPEAKER,
	GLYPH_SPEAKER_MUTED,
	GLYPH_STEP_BACK,
	GLYPH_STEP_FORWARD
};

namespace metal {

// The aluminium texture, made once. It tiles in both directions.
const BBitmap* texture();

// Fills `rect` of `view` with brushed metal. `origin` is where the view
// sits in the surface the texture should line up across (the window).
void fill(BView* view, BRect rect, BPoint origin = BPoint(0, 0));

// A light from the top: the window's metal gets a faint sheen and shade.
// `surface` is the whole metal surface in the view's coordinates, so that
// neighbouring views shade alike.
void sheen(BView* view, BRect rect, BRect surface);

// A groove cut into the metal around `rect`.
void inset_frame(BView* view, BRect rect, float radius = 0);

void round_button(BView* view, BRect rect, button_state state,
	bool prominent = false);
void glyph(BView* view, glyph_kind kind, BRect rect, rgb_color color);

// The timeline: a recessed track, the played part, chapter marks and the
// triangular playhead QuickTime used.
void timeline_track(BView* view, BRect track, float playedTo, bool dark);
void playhead(BView* view, BPoint tip, float height, button_state state,
	bool dark);

void slider_track(BView* view, BRect track, float fraction, bool dark);
void slider_knob(BView* view, BPoint center, button_state state, bool dark);

// The LCD panel and the colour of what is written on it.
void lcd(BView* view, BRect rect);
rgb_color lcd_text_color();
rgb_color lcd_dim_text_color();

// The full screen controller's dark glass.
void hud_panel(BView* view, BRect rect);

rgb_color color(uint8 red, uint8 green, uint8 blue, uint8 alpha = 255);
rgb_color mix(rgb_color a, rgb_color b, float amount);

}	// namespace metal

}	// namespace airtime

#endif	// AIRTIME_METAL_H
