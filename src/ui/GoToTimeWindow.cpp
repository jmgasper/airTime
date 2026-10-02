/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "GoToTimeWindow.h"

#include <Beep.h>
#include <Button.h>
#include <LayoutBuilder.h>
#include <StringView.h>
#include <TextControl.h>

#include "Messages.h"
#include "Tracks.h"


namespace airtime {

static const uint32 kMsgOK = 'gtok';
static const uint32 kMsgChanged = 'gtch';


GoToTimeWindow::GoToTimeWindow(BWindow* owner, bigtime_t current,
	bigtime_t duration)
	:
	BWindow(BRect(0, 0, 300, 100), "Go to Time", B_MODAL_WINDOW_LOOK,
		B_MODAL_SUBSET_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS | B_CLOSE_ON_ESCAPE),
	fOwner(owner),
	fDuration(duration)
{
	AddToSubset(owner);

	fTime = new BTextControl("time", "Go to:",
		format_time(current, true, duration >= 3600000000LL).String(), NULL);
	fTime->SetModificationMessage(new BMessage(kMsgChanged));
	BString hint;
	if (duration > 0) {
		hint.SetToFormat("hh:mm:ss.ff, up to %s",
			format_time(duration, true).String());
	} else
		hint = "hh:mm:ss.ff";
	fHint = new BStringView("hint", hint.String());
	BFont font(be_plain_font);
	font.SetSize(font.Size() * 0.85f);
	fHint->SetFont(&font);
	fHint->SetHighUIColor(B_PANEL_TEXT_COLOR, B_LIGHTEN_1_TINT);

	BButton* cancel = new BButton("cancel", "Cancel",
		new BMessage(B_QUIT_REQUESTED));
	BButton* ok = new BButton("ok", "Go", new BMessage(kMsgOK));

	BLayoutBuilder::Group<>(this, B_VERTICAL)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fTime)
		.Add(fHint)
		.AddGroup(B_HORIZONTAL)
			.AddGlue()
			.Add(cancel)
			.Add(ok)
		.End();
	SetDefaultButton(ok);
	fTime->MakeFocus(true);
	fTime->TextView()->SelectAll();
	CenterIn(owner->Frame());
}


void
GoToTimeWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgChanged:
		{
			bigtime_t time;
			bool valid = parse_time(fTime->Text(), &time)
				&& (fDuration <= 0 || time <= fDuration);
			fTime->MarkAsInvalid(!valid);
			break;
		}
		case kMsgOK:
		{
			bigtime_t time;
			if (!parse_time(fTime->Text(), &time)) {
				fTime->MarkAsInvalid(true);
				beep();
				break;
			}
			if (fDuration > 0 && time > fDuration)
				time = fDuration;
			BMessage go(kMsgGoToTimeDone);
			go.AddInt64("time", time);
			fOwner.SendMessage(&go);
			Quit();
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}

}	// namespace airtime
