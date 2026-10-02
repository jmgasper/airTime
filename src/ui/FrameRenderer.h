/*
 * Copyright 2026, air/OS contributors.
 * Distributed under the terms of the MIT License.
 */
#ifndef AIRTIME_FRAME_RENDERER_H
#define AIRTIME_FRAME_RENDERER_H


#include "FFmpeg.h"


namespace airtime {

/*!	Converts a decoded picture, in whatever format the decoder produced, to
	B_RGB32 pixels of the size it is shown at, in one swscale pass spread
	over several threads. */
class FrameRenderer {
public:
								FrameRenderer();
								~FrameRenderer();

			bool				Render(const AVFrame* frame, uint8* bits,
									int32 bytesPerRow, int width, int height);

private:
			bool				_Prepare(const AVFrame* frame, int width,
									int height);

			SwsContext*			fContext;
			int					fSourceWidth;
			int					fSourceHeight;
			int					fSourceFormat;
			int					fColorSpace;
			int					fRange;
			int					fWidth;
			int					fHeight;
			AVFrame*			fSource;
			AVFrame*			fTarget;
};

}	// namespace airtime

#endif	// AIRTIME_FRAME_RENDERER_H
