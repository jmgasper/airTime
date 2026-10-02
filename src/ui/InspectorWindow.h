/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_INSPECTOR_WINDOW_H
#define AIRTIME_INSPECTOR_WINDOW_H


#include <vector>

#include <Messenger.h>
#include <Window.h>


class BStringView;


namespace airtime {

enum {
	kMsgInspectorUpdate = 'iupd'	// "label" and "value" strings, in pairs
};

/*!	QuickTime's Movie Inspector: what the file is and how it is playing,
	including which decoder (the graphics hardware or the processor) is
	making the pictures. */
class InspectorWindow : public BWindow {
public:
								InspectorWindow(BRect frame,
									const BMessenger& owner);

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_Update(BMessage* message);

			BMessenger			fOwner;
			BView*				fBackground;
			std::vector<BStringView*> fLabels;
			std::vector<BStringView*> fValues;
};

}	// namespace airtime

#endif	// AIRTIME_INSPECTOR_WINDOW_H
