#ifndef PA_OPENSLES_H
#define PA_OPENSLES_H

/*
 * $Id:
 * PortAudio Portable Real-Time Audio Library
 * Android OpenSLES-specific extensions
 *
 * Copyright (c) 2016-2017 Sanne Raymaekers
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge,
 * publish, distribute, sublicense, and/or sell copies of the Software,
 * and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The text above constitutes the entire PortAudio license; however,
 * the PortAudio community also makes the following non-binding requests:
 *
 * Any person wishing to distribute modifications to the Software is
 * requested to send the modifications to the original developer so that
 * they can be incorporated into the canonical version. It is also
 * requested that these non-binding requests be included along with the
 * license above.
 */

/** @file
 @ingroup public_header
 @brief Android OpenSLES-specific PortAudio API extension header file.
*/

#include <SLES/OpenSLES.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PaOpenslesStreamInfo {
    SLint32 androidPlaybackStreamType;
} PaOpenslesStreamInfo;

/* 
 * Provide PA OpenSLES with native buffer information. This function must be called before Pa_Initialize.
 * To have optimal latency, this function should be called. Otherwise PA OpenSLES will use non-optimal values
 * as default.
 */
void SetNativeBufferSize( unsigned long bufferSize );

#ifdef __cplusplus
}
#endif

#endif
