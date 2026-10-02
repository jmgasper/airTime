/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_VIDEO_VIEW_H
#define AIRTIME_VIDEO_VIEW_H


#include <atomic>
#include <mutex>

#include <Messenger.h>
#include <String.h>
#include <View.h>

#include "FrameRenderer.h"
#include "Player.h"
#include "SubtitleRenderer.h"


class BBitmap;


namespace airtime {

/*!	Where the film is shown. Pictures arrive on the player's presentation
	thread, are converted to the size they are shown at, get their subtitles
	drawn in, and are put on screen from there; Draw() only repaints what is
	already made (and, while paused, makes it again at a new size). */
class VideoView : public BView, public VideoSink {
public:
								VideoView(BRect frame,
									const BMessenger& target);
	virtual						~VideoView();

			void				SetPlayer(Player* player);
			void				SetAudioOnly(bool audioOnly);
			void				SetInfo(const char* title,
									const char* detail);
			void				SetSubtitleScale(float scale);
			void				ShowMessage(const char* text);
			// Draw the last picture again (subtitles or size changed).
			void				Refresh();
			void				SetFullScreen(bool fullScreen);
			BRect				VideoFrame();

	// VideoSink, on the presentation thread
	virtual	bool				DisplayFrame(const VideoFramePtr& frame);
	virtual	void				GetTimings(bigtime_t* compose,
									bigtime_t* draw);

	// BView
	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				FrameResized(float width, float height);
	virtual	void				MouseDown(BPoint where);
	virtual	void				MouseMoved(BPoint where, uint32 transit,
									const BMessage* dragMessage);
	virtual	void				KeyDown(const char* bytes, int32 count);
	virtual	void				MessageReceived(BMessage* message);

private:
			BRect				_VideoRectFor(BRect bounds) const;
			bool				_Compose(const VideoFramePtr& frame,
									int index, int width, int height);
			void				_DrawEmpty(BRect updateRect);
			void				_DrawAudioOnly(BRect updateRect);
			void				_Post(uint32 what);

			BMessenger			fTarget;
			Player*				fPlayer;

			std::mutex			fGeometryLock;
			BRect				fVideoRect;

			std::mutex			fRenderLock;
			FrameRenderer		fRenderer;
			SubtitleRenderer	fSubtitles;
			BBitmap*			fBitmaps[2];
			std::atomic<int>	fCurrent;
			VideoFramePtr		fLastFrame;
			std::atomic<bool>	fDirty;

			std::mutex			fMessageLock;
			BString				fMessage;
			bigtime_t			fMessageUntil;

			bool				fAudioOnly;
			bool				fFullScreen;
			BString				fTitle;
			BString				fDetail;
			BPoint				fLastMouse;
			std::atomic<bigtime_t> fComposeTime;
			std::atomic<bigtime_t> fDrawTime;
};

}	// namespace airtime

#endif	// AIRTIME_VIDEO_VIEW_H
