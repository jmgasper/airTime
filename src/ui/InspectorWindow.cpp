/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */


#include "InspectorWindow.h"

#include <LayoutBuilder.h>
#include <GridLayout.h>
#include <StringView.h>

#include "Messages.h"


namespace airtime {


InspectorWindow::InspectorWindow(BRect frame, const BMessenger& owner)
	:
	BWindow(frame, "Movie Inspector", B_FLOATING_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_ZOOMABLE | B_AUTO_UPDATE_SIZE_LIMITS
			| B_ASYNCHRONOUS_CONTROLS | B_CLOSE_ON_ESCAPE),
	fOwner(owner)
{
	fBackground = new BView("background", B_WILL_DRAW);
	fBackground->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	BGridLayout* grid = new BGridLayout(B_USE_HALF_ITEM_SPACING, 2);
	fBackground->SetLayout(grid);
	grid->SetInsets(B_USE_WINDOW_SPACING);
	BLayoutBuilder::Group<>(this, B_VERTICAL, 0)
		.Add(fBackground);
}


void
InspectorWindow::_Update(BMessage* message)
{
	BGridLayout* grid = (BGridLayout*)fBackground->GetLayout();
	const char* label;
	const char* value;
	size_t index = 0;
	for (int32 i = 0; message->FindString("label", i, &label) == B_OK
			&& message->FindString("value", i, &value) == B_OK; i++) {
		if (index >= fLabels.size()) {
			BStringView* labelView = new BStringView(NULL, "");
			BFont font(be_bold_font);
			labelView->SetFont(&font);
			labelView->SetAlignment(B_ALIGN_RIGHT);
			BStringView* valueView = new BStringView(NULL, "");
			valueView->SetExplicitMinSize(BSize(260, B_SIZE_UNSET));
			grid->AddView(labelView, 0, index);
			grid->AddView(valueView, 1, index);
			fLabels.push_back(labelView);
			fValues.push_back(valueView);
		}
		if (strcmp(fLabels[index]->Text(), label) != 0)
			fLabels[index]->SetText(label);
		if (strcmp(fValues[index]->Text(), value) != 0)
			fValues[index]->SetText(value);
		fLabels[index]->Show();
		fValues[index]->Show();
		index++;
	}
	for (size_t i = index; i < fLabels.size(); i++) {
		if (!fLabels[i]->IsHidden(fLabels[i])) {
			fLabels[i]->Hide();
			fValues[i]->Hide();
		}
	}
}


void
InspectorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgInspectorUpdate:
		{
			BString title;
			if (message->FindString("title", &title) == B_OK) {
				BString windowTitle("Movie Inspector — ");
				windowTitle << title;
				SetTitle(windowTitle.String());
			}
			_Update(message);
			break;
		}
		default:
			BWindow::MessageReceived(message);
	}
}


bool
InspectorWindow::QuitRequested()
{
	BMessage message(kMsgInspectorClosed);
	message.AddRect("frame", Frame());
	fOwner.SendMessage(&message);
	return true;
}

}	// namespace airtime
