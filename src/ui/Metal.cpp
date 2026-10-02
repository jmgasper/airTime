/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "Metal.h"

#include <math.h>
#include <mutex>
#include <vector>

#include <Bitmap.h>
#include <Gradient.h>
#include <GradientLinear.h>
#include <GradientRadial.h>
#include <Region.h>
#include <Shape.h>
#include <View.h>


namespace airtime {
namespace metal {


rgb_color
color(uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	rgb_color result;
	result.red = red;
	result.green = green;
	result.blue = blue;
	result.alpha = alpha;
	return result;
}


rgb_color
mix(rgb_color a, rgb_color b, float amount)
{
	rgb_color result;
	result.red = (uint8)(a.red + (b.red - a.red) * amount);
	result.green = (uint8)(a.green + (b.green - a.green) * amount);
	result.blue = (uint8)(a.blue + (b.blue - a.blue) * amount);
	result.alpha = (uint8)(a.alpha + (b.alpha - a.alpha) * amount);
	return result;
}


// #pragma mark - texture


static std::once_flag sTextureOnce;
static BBitmap* sTexture = NULL;


static void
make_texture()
{
	const int width = 512;
	const int height = 256;
	sTexture = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);

	uint32 seed = 0x2468ace1;
	auto random = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return ((seed >> 8) & 0xffff) / 65535.0f * 2.0f - 1.0f;
	};

	std::vector<float> noise(width);
	std::vector<float> streak(width);
	float rowDrift = 0;
	uint8* bits = (uint8*)sTexture->Bits();
	int32 bytesPerRow = sTexture->BytesPerRow();

	for (int y = 0; y < height; y++) {
		// Brushing leaves long horizontal streaks, different on every row,
		// and a slow drift in brightness from row to row.
		for (int x = 0; x < width; x++)
			noise[x] = random();
		for (int pass = 0; pass < 2; pass++) {
			const int length = pass == 0 ? 41 : 23;
			float sum = 0;
			for (int i = -length / 2; i <= length / 2; i++)
				sum += noise[(i + width) % width];
			for (int x = 0; x < width; x++) {
				streak[x] = sum / length;
				sum += noise[(x + length / 2 + 1) % width];
				sum -= noise[(x - length / 2 + width) % width];
			}
			noise = streak;
		}
		rowDrift = rowDrift * 0.82f + random() * 1.4f;
		float rowBase = 204.0f + rowDrift + random() * 2.2f;

		uint8* row = bits + y * bytesPerRow;
		for (int x = 0; x < width; x++) {
			float value = rowBase + streak[x] * 38.0f + random() * 1.6f;
			if (value < 0)
				value = 0;
			if (value > 255)
				value = 255;
			uint8 gray = (uint8)value;
			row[x * 4 + 0] = gray < 253 ? gray + 2 : 255;	// a cool tint
			row[x * 4 + 1] = gray;
			row[x * 4 + 2] = gray > 1 ? gray - 1 : 0;
			row[x * 4 + 3] = 255;
		}
	}
}


const BBitmap*
texture()
{
	std::call_once(sTextureOnce, make_texture);
	return sTexture;
}


void
fill(BView* view, BRect rect, BPoint origin)
{
	const BBitmap* tile = texture();
	BRect bounds = tile->Bounds();
	float tileWidth = bounds.Width() + 1;
	float tileHeight = bounds.Height() + 1;

	// Tiles lined up with the surface: start left of and above the rect.
	float startX = floorf((rect.left + origin.x) / tileWidth) * tileWidth
		- origin.x;
	float startY = floorf((rect.top + origin.y) / tileHeight) * tileHeight
		- origin.y;
	view->PushState();
	view->SetDrawingMode(B_OP_COPY);
	BRegion clip(rect);
	view->ConstrainClippingRegion(&clip);
	for (float y = startY; y <= rect.bottom; y += tileHeight) {
		for (float x = startX; x <= rect.right; x += tileWidth) {
			view->DrawBitmap(tile, BPoint(x, y));
		}
	}
	view->PopState();
}


void
sheen(BView* view, BRect rect, BRect surface)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	BGradientLinear gradient(BPoint(0, surface.top), BPoint(0, surface.bottom));
	gradient.AddColor(color(255, 255, 255, 70), 0);
	gradient.AddColor(color(255, 255, 255, 0), 110);
	gradient.AddColor(color(0, 0, 0, 0), 150);
	gradient.AddColor(color(0, 0, 0, 34), 255);
	view->FillRect(rect, gradient);
	view->PopState();
}


void
inset_frame(BView* view, BRect rect, float radius)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetPenSize(1);
	BRect outer = rect.InsetByCopy(-1, -1);
	if (radius > 0) {
		view->SetHighColor(color(255, 255, 255, 150));
		view->StrokeRoundRect(outer.OffsetByCopy(0, 1), radius, radius);
		view->SetHighColor(color(40, 40, 40, 170));
		view->StrokeRoundRect(outer, radius, radius);
	} else {
		view->SetHighColor(color(40, 40, 40, 150));
		view->StrokeLine(outer.LeftBottom(), outer.LeftTop());
		view->StrokeLine(outer.LeftTop(), outer.RightTop());
		view->SetHighColor(color(255, 255, 255, 170));
		view->StrokeLine(outer.RightTop() + BPoint(0, 1), outer.RightBottom());
		view->StrokeLine(outer.RightBottom(), outer.LeftBottom()
			+ BPoint(1, 0));
	}
	view->PopState();
}


// #pragma mark - buttons


void
round_button(BView* view, BRect rect, button_state state, bool prominent)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

	// The hole the button sits in.
	BRect well = rect.InsetByCopy(-1, -1);
	BGradientLinear wellGradient(well.LeftTop(), well.LeftBottom());
	wellGradient.AddColor(color(0, 0, 0, 90), 0);
	wellGradient.AddColor(color(255, 255, 255, 160), 255);
	view->FillEllipse(well, wellGradient);

	// The rim.
	BGradientLinear rim(rect.LeftTop(), rect.LeftBottom());
	rim.AddColor(color(96, 96, 98), 0);
	rim.AddColor(color(58, 58, 60), 255);
	view->FillEllipse(rect, rim);

	// The face, lit from above; pressed, it turns its shade upward.
	BRect face = rect.InsetByCopy(prominent ? 1.6f : 1.2f,
		prominent ? 1.6f : 1.2f);
	BGradientLinear faceGradient(face.LeftTop(), face.LeftBottom());
	switch (state) {
		case BUTTON_PRESSED:
			faceGradient.AddColor(color(150, 150, 154), 0);
			faceGradient.AddColor(color(196, 196, 200), 160);
			faceGradient.AddColor(color(222, 222, 226), 255);
			break;
		case BUTTON_HOVER:
			faceGradient.AddColor(color(255, 255, 255), 0);
			faceGradient.AddColor(color(232, 232, 236), 120);
			faceGradient.AddColor(color(196, 196, 202), 255);
			break;
		case BUTTON_DISABLED:
			faceGradient.AddColor(color(226, 226, 226), 0);
			faceGradient.AddColor(color(200, 200, 200), 255);
			break;
		default:
			faceGradient.AddColor(color(250, 250, 252), 0);
			faceGradient.AddColor(color(222, 222, 226), 120);
			faceGradient.AddColor(color(178, 178, 184), 255);
			break;
	}
	view->FillEllipse(face, faceGradient);

	// A soft reflection in the upper half.
	if (state != BUTTON_PRESSED) {
		BRect gloss = face.InsetByCopy(face.Width() * 0.18f, 0);
		gloss.top += face.Height() * 0.06f;
		gloss.bottom = gloss.top + face.Height() * 0.42f;
		BGradientLinear glossGradient(gloss.LeftTop(), gloss.LeftBottom());
		glossGradient.AddColor(color(255, 255, 255, 170), 0);
		glossGradient.AddColor(color(255, 255, 255, 0), 255);
		view->FillEllipse(gloss, glossGradient);
	}
	view->PopState();
}


static BPoint
map_point(BRect rect, float x, float y)
{
	return BPoint(rect.left + x * rect.Width(), rect.top + y * rect.Height());
}


static void
fill_polygon(BView* view, BRect rect, const float* points, int count)
{
	BShape shape;
	shape.MoveTo(map_point(rect, points[0], points[1]));
	for (int i = 1; i < count; i++)
		shape.LineTo(map_point(rect, points[i * 2], points[i * 2 + 1]));
	shape.Close();
	view->FillShape(&shape);
}


static void
fill_unit_rect(BView* view, BRect rect, float left, float top, float right,
	float bottom)
{
	BRect bar(map_point(rect, left, top), map_point(rect, right, bottom));
	view->FillRect(bar);
}


void
glyph(BView* view, glyph_kind kind, BRect rect, rgb_color glyphColor)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(glyphColor);
	view->SetLowColor(glyphColor);

	switch (kind) {
		case GLYPH_PLAY:
		{
			const float points[] = {0.32f, 0.20f, 0.32f, 0.80f, 0.82f, 0.50f};
			fill_polygon(view, rect, points, 3);
			break;
		}
		case GLYPH_PAUSE:
			fill_unit_rect(view, rect, 0.27f, 0.22f, 0.43f, 0.78f);
			fill_unit_rect(view, rect, 0.57f, 0.22f, 0.73f, 0.78f);
			break;
		case GLYPH_REWIND:
		{
			const float a[] = {0.50f, 0.26f, 0.50f, 0.74f, 0.13f, 0.50f};
			const float b[] = {0.87f, 0.26f, 0.87f, 0.74f, 0.50f, 0.50f};
			fill_polygon(view, rect, a, 3);
			fill_polygon(view, rect, b, 3);
			break;
		}
		case GLYPH_FAST_FORWARD:
		{
			const float a[] = {0.13f, 0.26f, 0.13f, 0.74f, 0.50f, 0.50f};
			const float b[] = {0.50f, 0.26f, 0.50f, 0.74f, 0.87f, 0.50f};
			fill_polygon(view, rect, a, 3);
			fill_polygon(view, rect, b, 3);
			break;
		}
		case GLYPH_TO_START:
		{
			fill_unit_rect(view, rect, 0.20f, 0.26f, 0.30f, 0.74f);
			const float a[] = {0.56f, 0.26f, 0.56f, 0.74f, 0.30f, 0.50f};
			const float b[] = {0.82f, 0.26f, 0.82f, 0.74f, 0.56f, 0.50f};
			fill_polygon(view, rect, a, 3);
			fill_polygon(view, rect, b, 3);
			break;
		}
		case GLYPH_TO_END:
		{
			fill_unit_rect(view, rect, 0.70f, 0.26f, 0.80f, 0.74f);
			const float a[] = {0.18f, 0.26f, 0.18f, 0.74f, 0.44f, 0.50f};
			const float b[] = {0.44f, 0.26f, 0.44f, 0.74f, 0.70f, 0.50f};
			fill_polygon(view, rect, a, 3);
			fill_polygon(view, rect, b, 3);
			break;
		}
		case GLYPH_STEP_BACK:
		{
			fill_unit_rect(view, rect, 0.28f, 0.28f, 0.38f, 0.72f);
			const float a[] = {0.74f, 0.28f, 0.74f, 0.72f, 0.40f, 0.50f};
			fill_polygon(view, rect, a, 3);
			break;
		}
		case GLYPH_STEP_FORWARD:
		{
			fill_unit_rect(view, rect, 0.62f, 0.28f, 0.72f, 0.72f);
			const float a[] = {0.26f, 0.28f, 0.26f, 0.72f, 0.60f, 0.50f};
			fill_polygon(view, rect, a, 3);
			break;
		}
		case GLYPH_FULL_SCREEN:
		case GLYPH_EXIT_FULL_SCREEN:
		{
			// Four corners, pointing out (or in, to leave).
			bool out = kind == GLYPH_FULL_SCREEN;
			float pen = rect.Width() / 9.0f;
			if (pen < 1.5f)
				pen = 1.5f;
			view->SetPenSize(pen);
			view->SetLineMode(B_SQUARE_CAP, B_MITER_JOIN);
			const float near = 0.18f, far = 0.82f, arm = 0.22f;
			for (int corner = 0; corner < 4; corner++) {
				float cx = corner & 1 ? far : near;
				float cy = corner & 2 ? far : near;
				float dx = corner & 1 ? -1 : 1;
				float dy = corner & 2 ? -1 : 1;
				if (!out) {
					// The corner moves inward and the arms point outward.
					cx += dx * arm;
					cy += dy * arm;
					dx = -dx;
					dy = -dy;
				}
				BShape shape;
				shape.MoveTo(map_point(rect, cx + dx * arm, cy));
				shape.LineTo(map_point(rect, cx, cy));
				shape.LineTo(map_point(rect, cx, cy + dy * arm));
				view->StrokeShape(&shape);
			}
			break;
		}
		case GLYPH_SPEAKER:
		case GLYPH_SPEAKER_MUTED:
		{
			fill_unit_rect(view, rect, 0.10f, 0.38f, 0.26f, 0.62f);
			const float cone[] = {0.24f, 0.38f, 0.48f, 0.16f, 0.48f, 0.84f,
				0.24f, 0.62f};
			fill_polygon(view, rect, cone, 4);
			float pen = rect.Width() / 12.0f;
			if (pen < 1.2f)
				pen = 1.2f;
			view->SetPenSize(pen);
			if (kind == GLYPH_SPEAKER) {
				BPoint center = map_point(rect, 0.46f, 0.50f);
				for (int wave = 0; wave < 2; wave++) {
					float radius = rect.Width() * (0.20f + wave * 0.17f);
					view->StrokeArc(center, radius, radius, -45, 90);
				}
			} else {
				view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);
				view->StrokeLine(map_point(rect, 0.60f, 0.34f),
					map_point(rect, 0.88f, 0.66f));
				view->StrokeLine(map_point(rect, 0.60f, 0.66f),
					map_point(rect, 0.88f, 0.34f));
			}
			break;
		}
	}
	view->PopState();
}


// #pragma mark - timeline and sliders


void
timeline_track(BView* view, BRect track, float playedTo, bool dark)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	float radius = track.Height() / 2;

	if (dark) {
		view->SetHighColor(color(20, 20, 20, 230));
		view->FillRoundRect(track, radius, radius);
		view->SetHighColor(color(255, 255, 255, 40));
		view->StrokeRoundRect(track.OffsetByCopy(0, 1), radius, radius);
	} else {
		// A groove: dark above, catching light below.
		BRect outer = track.InsetByCopy(-1, -1);
		BGradientLinear edge(outer.LeftTop(), outer.LeftBottom());
		edge.AddColor(color(60, 60, 64, 200), 0);
		edge.AddColor(color(255, 255, 255, 200), 255);
		view->FillRoundRect(outer, radius + 1, radius + 1, edge);
		BGradientLinear inside(track.LeftTop(), track.LeftBottom());
		inside.AddColor(color(176, 178, 182), 0);
		inside.AddColor(color(226, 228, 232), 120);
		inside.AddColor(color(244, 245, 248), 255);
		view->FillRoundRect(track, radius, radius, inside);
	}

	if (playedTo > track.left + 1) {
		BRect played = track;
		played.right = playedTo < track.right ? playedTo : track.right;
		BGradientLinear fillGradient(played.LeftTop(), played.LeftBottom());
		if (dark) {
			fillGradient.AddColor(color(230, 230, 234), 0);
			fillGradient.AddColor(color(170, 170, 176), 255);
		} else {
			fillGradient.AddColor(color(88, 98, 112), 0);
			fillGradient.AddColor(color(128, 140, 156), 140);
			fillGradient.AddColor(color(156, 168, 184), 255);
		}
		view->FillRoundRect(played, radius, radius, fillGradient);
	}
	view->PopState();
}


void
playhead(BView* view, BPoint tip, float height, button_state state, bool dark)
{
	// QuickTime's playhead: a little metal pentagon pointing down at the
	// track.
	float width = roundf(height * 0.8f);
	float shoulder = height * 0.52f;
	auto outline = [&](BShape& shape, float dx, float dy) {
		shape.MoveTo(BPoint(tip.x - width / 2 + dx, tip.y - height + dy));
		shape.LineTo(BPoint(tip.x + width / 2 + dx, tip.y - height + dy));
		shape.LineTo(BPoint(tip.x + width / 2 + dx,
			tip.y - height + shoulder + dy));
		shape.LineTo(BPoint(tip.x + dx, tip.y + dy));
		shape.LineTo(BPoint(tip.x - width / 2 + dx,
			tip.y - height + shoulder + dy));
		shape.Close();
	};
	BShape shape;
	outline(shape, 0, 0);
	BShape shadow;
	outline(shadow, 0.8f, 1.2f);

	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	view->SetHighColor(color(0, 0, 0, 60));
	view->FillShape(&shadow);

	BGradientLinear face(BPoint(tip.x - width / 2, 0),
		BPoint(tip.x + width / 2, 0));
	bool pressed = state == BUTTON_PRESSED;
	if (dark) {
		face.AddColor(color(250, 250, 250), 0);
		face.AddColor(color(200, 200, 204), 255);
	} else {
		face.AddColor(pressed ? color(170, 170, 176) : color(252, 252, 252), 0);
		face.AddColor(pressed ? color(140, 140, 146) : color(206, 206, 212),
			140);
		face.AddColor(pressed ? color(120, 120, 126) : color(164, 164, 170),
			255);
	}
	view->FillShape(&shape, face);
	view->SetHighColor(color(40, 40, 44, 220));
	view->SetPenSize(1);
	view->StrokeShape(&shape);
	view->PopState();
}


void
slider_track(BView* view, BRect track, float fraction, bool dark)
{
	float played = track.left + (track.Width()) * fraction;
	timeline_track(view, track, played, dark);
}


void
slider_knob(BView* view, BPoint center, button_state state, bool dark)
{
	BRect knob(center.x - 6, center.y - 6, center.x + 6, center.y + 6);
	if (dark) {
		view->PushState();
		view->SetDrawingMode(B_OP_ALPHA);
		BGradientLinear face(knob.LeftTop(), knob.LeftBottom());
		face.AddColor(color(250, 250, 250), 0);
		face.AddColor(color(190, 190, 196), 255);
		view->FillEllipse(knob, face);
		view->PopState();
		return;
	}
	round_button(view, knob, state, false);
}


// #pragma mark - LCD and HUD


rgb_color
lcd_text_color()
{
	return color(36, 42, 28);
}


rgb_color
lcd_dim_text_color()
{
	return color(88, 98, 76);
}


void
lcd(BView* view, BRect rect)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	float radius = 5;

	// The bezel: cut into the metal.
	BRect bezel = rect.InsetByCopy(-1, -1);
	BGradientLinear bezelGradient(bezel.LeftTop(), bezel.LeftBottom());
	bezelGradient.AddColor(color(70, 72, 66, 230), 0);
	bezelGradient.AddColor(color(255, 255, 255, 210), 255);
	view->FillRoundRect(bezel, radius + 1, radius + 1, bezelGradient);

	// The glass, khaki and slightly lit from below like the old LCD.
	BGradientLinear glass(rect.LeftTop(), rect.LeftBottom());
	glass.AddColor(color(176, 186, 156), 0);
	glass.AddColor(color(200, 208, 182), 90);
	glass.AddColor(color(214, 220, 198), 255);
	view->FillRoundRect(rect, radius, radius, glass);

	// Its inner shadow.
	BRect shade = rect.InsetByCopy(1, 1);
	shade.bottom = shade.top + 4;
	BGradientLinear shadeGradient(shade.LeftTop(), shade.LeftBottom());
	shadeGradient.AddColor(color(0, 0, 0, 60), 0);
	shadeGradient.AddColor(color(0, 0, 0, 0), 255);
	view->FillRoundRect(shade, radius - 1, radius - 1, shadeGradient);

	view->SetHighColor(color(60, 64, 52, 200));
	view->StrokeRoundRect(rect, radius, radius);
	view->PopState();
}


void
hud_panel(BView* view, BRect rect)
{
	view->PushState();
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
	float radius = 10;
	BGradientLinear glass(rect.LeftTop(), rect.LeftBottom());
	glass.AddColor(color(62, 62, 66), 0);
	glass.AddColor(color(34, 34, 38), 120);
	glass.AddColor(color(22, 22, 26), 255);
	view->FillRoundRect(rect, radius, radius, glass);
	view->SetHighColor(color(255, 255, 255, 50));
	view->StrokeRoundRect(rect.InsetByCopy(1, 1), radius - 1, radius - 1);
	view->SetHighColor(color(0, 0, 0, 200));
	view->StrokeRoundRect(rect, radius, radius);
	view->PopState();
}

}	// namespace metal
}	// namespace airtime
