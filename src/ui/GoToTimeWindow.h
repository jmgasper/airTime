/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_GO_TO_TIME_WINDOW_H
#define AIRTIME_GO_TO_TIME_WINDOW_H


#include <Messenger.h>
#include <Window.h>


class BStringView;
class BTextControl;


namespace airtime {

/*!	Asks for a time to jump to: "1:02:03", "62:03.5", "1h2m3s" or plain
	seconds. */
class GoToTimeWindow : public BWindow {
public:
								GoToTimeWindow(BWindow* owner,
									bigtime_t current, bigtime_t duration);

	virtual	void				MessageReceived(BMessage* message);

private:
			BMessenger			fOwner;
			bigtime_t			fDuration;
			BTextControl*		fTime;
			BStringView*		fHint;
};

}	// namespace airtime

#endif	// AIRTIME_GO_TO_TIME_WINDOW_H
