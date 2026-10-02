/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "VideoView.h"

#include <math.h>

#include <Bitmap.h>
#include <InterfaceDefs.h>
#include <Message.h>
#include <Region.h>
#include <Window.h>

#include "Messages.h"
#include "Metal.h"


namespace airtime {


VideoView::VideoView(BRect frame, const BMessenger& target)
	:
	BView(frame, "video", B_FOLLOW_ALL, B_WILL_DRAW | B_FRAME_EVENTS
		| B_NAVIGABLE | B_FULL_UPDATE_ON_RESIZE),
	fTarget(target),
	fPlayer(NULL),
	fCurrent(0),
	fDirty(false),
	fMessageUntil(0),
	fAudioOnly(false),
	fFullScreen(false),
	fComposeTime(0),
	fDrawTime(0)
{
	fBitmaps[0] = NULL;
	fBitmaps[1] = NULL;
	SetViewColor(B_TRANSPARENT_COLOR);
	SetHighColor(0, 0, 0);
	fVideoRect = Bounds();
}


VideoView::~VideoView()
{
	delete fBitmaps[0];
	delete fBitmaps[1];
}


void
VideoView::AttachedToWindow()
{
	BView::AttachedToWindow();
	MakeFocus(true);
}


void
VideoView::SetPlayer(Player* player)
{
	{
		std::lock_guard<std::mutex> render(fRenderLock);
		fPlayer = player;
		fLastFrame.reset();
		delete fBitmaps[0];
		delete fBitmaps[1];
		fBitmaps[0] = fBitmaps[1] = NULL;
	}
	{
		std::lock_guard<std::mutex> lock(fGeometryLock);
		fVideoRect = _VideoRectFor(Bounds());
	}
	Invalidate();
}


void
VideoView::SetAudioOnly(bool audioOnly)
{
	fAudioOnly = audioOnly;
	Invalidate();
}


void
VideoView::SetInfo(const char* title, const char* detail)
{
	fTitle = title;
	fDetail = detail;
	if (fAudioOnly || fLastFrame.get() == NULL)
		Invalidate();
}


void
VideoView::SetSubtitleScale(float scale)
{
	{
		std::lock_guard<std::mutex> render(fRenderLock);
		fSubtitles.SetScale(scale);
	}
	Refresh();
}


void
VideoView::ShowMessage(const char* text)
{
	{
		std::lock_guard<std::mutex> lock(fMessageLock);
		fMessage = text;
		fMessageUntil = system_time() + 1600000;
	}
	Refresh();
}


void
VideoView::Refresh()
{
	fDirty = true;
	if (fPlayer == NULL || !fPlayer->IsPlaying() || fPlayer->IsScanning())
		Invalidate(VideoFrame());
}


void
VideoView::SetFullScreen(bool fullScreen)
{
	fFullScreen = fullScreen;
}


BRect
VideoView::VideoFrame()
{
	std::lock_guard<std::mutex> lock(fGeometryLock);
	return fVideoRect;
}


BRect
VideoView::_VideoRectFor(BRect bounds) const
{
	float width = bounds.Width() + 1;
	float height = bounds.Height() + 1;
	if (fPlayer == NULL || !fPlayer->HasVideo() || width < 2 || height < 2)
		return bounds;
	double aspect = fPlayer->DisplayAspect();
	if (aspect <= 0)
		return bounds;
	float videoWidth = width;
	float videoHeight = height;
	if (width / height > aspect)
		videoWidth = roundf(height * aspect);
	else
		videoHeight = roundf(width / aspect);
	float left = floorf(bounds.left + (width - videoWidth) / 2);
	float top = floorf(bounds.top + (height - videoHeight) / 2);
	return BRect(left, top, left + videoWidth - 1, top + videoHeight - 1);
}


bool
VideoView::_Compose(const VideoFramePtr& frame, int index, int width,
	int height)
{
	// Called with fRenderLock held.
	BBitmap*& bitmap = fBitmaps[index];
	if (bitmap == NULL || bitmap->Bounds().IntegerWidth() + 1 != width
		|| bitmap->Bounds().IntegerHeight() + 1 != height) {
		delete bitmap;
		bitmap = new BBitmap(BRect(0, 0, width - 1, height - 1), B_RGB32);
		if (bitmap->InitCheck() != B_OK) {
			delete bitmap;
			bitmap = NULL;
			return false;
		}
	}
	uint8* bits = (uint8*)bitmap->Bits();
	int32 bytesPerRow = bitmap->BytesPerRow();
	if (!fRenderer.Render(frame->frame, bits, bytesPerRow, width, height))
		return false;

	if (fPlayer != NULL && fPlayer->HasMovingVideo()) {
		std::vector<SubtitleEventPtr> events
			= fPlayer->ActiveSubtitles(frame->pts);
		const std::vector<Overlay>& overlays = fSubtitles.Render(events, width,
			height);
		for (const Overlay& overlay : overlays)
			SubtitleRenderer::Blend(overlay, bits, bytesPerRow, width, height);
	}

	BString message;
	bigtime_t until;
	{
		std::lock_guard<std::mutex> lock(fMessageLock);
		message = fMessage;
		until = fMessageUntil;
	}
	bigtime_t now = system_time();
	if (message.Length() > 0 && now < until) {
		Overlay overlay;
		if (fSubtitles.RenderMessage(message.String(), width, height,
				overlay)) {
			bigtime_t left = until - now;
			uint8 opacity = left > 400000 ? 255 : (uint8)(left * 255 / 400000);
			SubtitleRenderer::Blend(overlay, bits, bytesPerRow, width, height,
				opacity);
		}
	}
	return true;
}


bool
VideoView::DisplayFrame(const VideoFramePtr& frame)
{
	BRect rect = VideoFrame();
	int width = rect.IntegerWidth() + 1;
	int height = rect.IntegerHeight() + 1;
	if (width < 2 || height < 2)
		return true;

	std::lock_guard<std::mutex> render(fRenderLock);
	int next = fCurrent ^ 1;
	bigtime_t start = system_time();
	if (!_Compose(frame, next, width, height))
		return true;
	fLastFrame = frame;
	bigtime_t composed = system_time();
	fComposeTime = (fComposeTime * 15 + (composed - start)) / 16;

	if (LockLooperWithTimeout(40000) != B_OK)
		return false;
	bigtime_t locked = system_time();
	BRect now = VideoFrame();
	if (now == rect)
		DrawBitmap(fBitmaps[next], rect.LeftTop());
	else {
		// Resized while this one was made: stretch it, the next fits.
		DrawBitmap(fBitmaps[next], fBitmaps[next]->Bounds(), now,
			B_FILTER_BITMAP_BILINEAR);
	}
	Sync();
	fCurrent = next;
	fDirty = false;
	UnlockLooper();
	fDrawTime = (fDrawTime * 15 + (system_time() - locked)) / 16;
	return true;
}


void
VideoView::GetTimings(bigtime_t* compose, bigtime_t* draw)
{
	*compose = fComposeTime;
	*draw = fDrawTime;
}


void
VideoView::Draw(BRect updateRect)
{
	if (fAudioOnly) {
		_DrawAudioOnly(updateRect);
		return;
	}
	BRect rect = VideoFrame();

	// Black around the picture.
	BRegion bars(Bounds());
	bars.Exclude(rect);
	SetHighColor(0, 0, 0);
	FillRegion(&bars);

	if (fPlayer == NULL || fLastFrame.get() == NULL) {
		if (fBitmaps[fCurrent] == NULL) {
			_DrawEmpty(updateRect);
			return;
		}
	}

	bool playing = fPlayer != NULL && fPlayer->IsPlaying()
		&& !fPlayer->IsScanning();
	if (fDirty && !playing && fLastFrame.get() != NULL
		&& fRenderLock.try_lock()) {
		int next = fCurrent ^ 1;
		if (_Compose(fLastFrame, next, rect.IntegerWidth() + 1,
				rect.IntegerHeight() + 1)) {
			fCurrent = next;
		}
		fDirty = false;
		fRenderLock.unlock();
	}

	BBitmap* bitmap = fBitmaps[fCurrent];
	if (bitmap == NULL) {
		FillRect(rect);
		return;
	}
	BRect bounds = bitmap->Bounds();
	if (bounds.Width() == rect.Width() && bounds.Height() == rect.Height())
		DrawBitmap(bitmap, rect.LeftTop());
	else
		DrawBitmap(bitmap, bounds, rect, B_FILTER_BITMAP_BILINEAR);
}


void
VideoView::_DrawEmpty(BRect updateRect)
{
	SetHighColor(0, 0, 0);
	FillRect(updateRect);
	BRect bounds = Bounds();
	if (bounds.Height() < 60)
		return;

	BFont font(be_bold_font);
	font.SetSize(15);
	SetFont(&font);
	SetDrawingMode(B_OP_OVER);
	const char* line1 = fTitle.Length() > 0 ? fTitle.String()
		: "Drop a movie here";
	const char* line2 = fDetail.Length() > 0 ? fDetail.String()
		: "or choose Open… from the File menu";
	SetHighColor(200, 200, 204);
	float center = (bounds.left + bounds.right) / 2;
	float middle = (bounds.top + bounds.bottom) / 2;
	DrawString(line1, BPoint(center - font.StringWidth(line1) / 2, middle - 4));
	font.SetSize(12);
	font.SetFace(B_REGULAR_FACE);
	SetFont(&font);
	SetHighColor(130, 130, 136);
	DrawString(line2, BPoint(center - font.StringWidth(line2) / 2, middle + 16));
	SetDrawingMode(B_OP_COPY);
}


void
VideoView::_DrawAudioOnly(BRect updateRect)
{
	BRect bounds = Bounds();
	BPoint origin = Frame().LeftTop();
	metal::fill(this, updateRect, origin);
	BRect surface = bounds;
	if (Parent() != NULL) {
		surface = Parent()->Bounds();
		surface.OffsetBy(-Frame().left, -Frame().top);
	}
	metal::sheen(this, updateRect, surface);

	BRect panel = bounds.InsetByCopy(12, 10);
	if (!panel.IsValid() || panel.Height() < 20)
		return;
	metal::lcd(this, panel);

	BFont font(be_bold_font);
	font.SetSize(15);
	SetFont(&font);
	SetDrawingMode(B_OP_OVER);
	font_height height;
	font.GetHeight(&height);
	BString title(fTitle);
	font.TruncateString(&title, B_TRUNCATE_END, panel.Width() - 24);
	SetHighColor(metal::lcd_text_color());
	float baseline = panel.top + 10 + height.ascent;
	DrawString(title.String(), BPoint(panel.left + 12, baseline));

	BFont small(be_plain_font);
	small.SetSize(12);
	SetFont(&small);
	BString detail(fDetail);
	small.TruncateString(&detail, B_TRUNCATE_END, panel.Width() - 24);
	SetHighColor(metal::lcd_dim_text_color());
	DrawString(detail.String(), BPoint(panel.left + 12, baseline + 20));
	SetDrawingMode(B_OP_COPY);
}


void
VideoView::FrameResized(float width, float height)
{
	{
		std::lock_guard<std::mutex> lock(fGeometryLock);
		fVideoRect = _VideoRectFor(Bounds());
	}
	fDirty = true;
	Invalidate();
}


void
VideoView::_Post(uint32 what)
{
	BMessage message(what);
	fTarget.SendMessage(&message);
}


void
VideoView::MouseDown(BPoint where)
{
	MakeFocus(true);
	BMessage* current = Window()->CurrentMessage();
	int32 buttons = B_PRIMARY_MOUSE_BUTTON;
	int32 clicks = 1;
	if (current != NULL) {
		current->FindInt32("buttons", &buttons);
		current->FindInt32("clicks", &clicks);
	}
	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		BMessage message('ctxm');
		message.AddPoint("where", ConvertToScreen(where));
		fTarget.SendMessage(&message);
		return;
	}
	if (fAudioOnly)
		return;
	BMessage message(kMsgVideoClicked);
	message.AddInt32("clicks", clicks);
	fTarget.SendMessage(&message);
}


void
VideoView::MouseMoved(BPoint where, uint32 transit,
	const BMessage* dragMessage)
{
	if (fFullScreen && (fabsf(where.x - fLastMouse.x) > 2
			|| fabsf(where.y - fLastMouse.y) > 2)) {
		BMessage message(kMsgControlsActivity);
		message.AddPoint("where", where);
		fTarget.SendMessage(&message);
	}
	fLastMouse = where;
}


void
VideoView::KeyDown(const char* bytes, int32 count)
{
	uint32 modifierKeys = modifiers();
	bool shift = (modifierKeys & B_SHIFT_KEY) != 0;
	bool option = (modifierKeys & B_OPTION_KEY) != 0;

	switch (bytes[0]) {
		case B_SPACE:
			_Post(kMsgTogglePlay);
			return;
		case B_LEFT_ARROW:
			if (option)
				_Post(kMsgGoToStart);
			else if (shift) {
				BMessage message(kMsgSeekBy);
				message.AddInt64("delta", -10000000);
				fTarget.SendMessage(&message);
			} else
				_Post(kMsgStepBackward);
			return;
		case B_RIGHT_ARROW:
			if (option)
				_Post(kMsgGoToEnd);
			else if (shift) {
				BMessage message(kMsgSeekBy);
				message.AddInt64("delta", 10000000);
				fTarget.SendMessage(&message);
			} else
				_Post(kMsgStepForward);
			return;
		case B_UP_ARROW:
			_Post(kMsgVolumeUp);
			return;
		case B_DOWN_ARROW:
			_Post(kMsgVolumeDown);
			return;
		case B_HOME:
			_Post(kMsgGoToStart);
			return;
		case B_END:
			_Post(kMsgGoToEnd);
			return;
		case B_PAGE_UP:
		case B_PAGE_DOWN:
		{
			BMessage message(kMsgChapter);
			message.AddInt32("step", bytes[0] == B_PAGE_UP ? -1 : 1);
			fTarget.SendMessage(&message);
			return;
		}
		case B_ESCAPE:
			_Post(kMsgExitFullScreen);
			return;
		case B_ENTER:
			if (option) {
				_Post(kMsgToggleFullScreen);
				return;
			}
			break;
		case 'f':
		case 'F':
			_Post(kMsgToggleFullScreen);
			return;
		case 'j':
		case 'J':
		case 'k':
		case 'K':
		case 'l':
		case 'L':
		{
			BMessage message(kMsgJKL);
			char key = bytes[0] | 0x20;
			message.AddInt32("key", key == 'j' ? -1 : key == 'k' ? 0 : 1);
			fTarget.SendMessage(&message);
			return;
		}
		case 'm':
		case 'M':
			_Post(kMsgToggleMute);
			return;
		case 'c':
		case 'C':
			_Post(kMsgToggleCaptions);
			return;
		case 's':
		case 'S':
			_Post(kMsgCycleSubtitle);
			return;
		case 'a':
		case 'A':
			_Post(kMsgCycleAudio);
			return;
		case ']':
			_Post(kMsgFaster);
			return;
		case '[':
			_Post(kMsgSlower);
			return;
		case '\\':
		{
			BMessage message(kMsgSetRate);
			message.AddDouble("rate", 1.0);
			fTarget.SendMessage(&message);
			return;
		}
		case 'i':
		case 'I':
			_Post(kMsgShowInspector);
			return;
	}
	BView::KeyDown(bytes, count);
}


void
VideoView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_SIMPLE_DATA:
		case B_REFS_RECEIVED:
			// Dropped files: the window decides what they are.
			fTarget.SendMessage(message);
			break;
		case B_MOUSE_WHEEL_CHANGED:
		{
			float delta = 0;
			if (message->FindFloat("be:wheel_delta_y", &delta) == B_OK
				&& delta != 0) {
				_Post(delta < 0 ? kMsgVolumeUp : kMsgVolumeDown);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}

}	// namespace airtime
