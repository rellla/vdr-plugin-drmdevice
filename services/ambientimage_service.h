// SPDX-License-Identifier: MIT

/**
 * @file ambientimage_service.h
 * Ambient Image Service Interface
 *
 * The output plugin provides a small RGB image of the displayed video for
 * ambient lighting plugins (e.g. vdr-plugin-hyperion). Another output plugin
 * may provide the same service, so the consumer doesn't depend on drmdevice.
 *
 * Usage (in the thread of the consumer, never in VDR's main thread):
 * @code
 *   cPlugin *provider = cPluginManager::CallFirstService(AMBIENTIMAGE_SERVICE);
 *
 *   uint8_t rgb[160 * 160 * 3];
 *   AmbientImage_v1_0_t req = {};
 *   req.structSize = sizeof(req);
 *   req.width = 80;
 *   req.rgb = rgb;
 *   req.rgbSize = sizeof(rgb);
 *   req.timeoutMs = 250;
 *   req.lastFrame = lastFrame;
 *   if (provider && provider->Service(AMBIENTIMAGE_SERVICE, &req) && (req.flags & AMBIENT_NEW_IMAGE))
 *       lastFrame = req.frame;   // rgb holds req.imageWidth x req.imageHeight pixels
 * @endcode
 *
 * The call waits up to timeoutMs for a frame, which is newer than lastFrame,
 * and returns its image. A call with Data == NULL returns true, if the service
 * is provided.
 *
 * The image:
 * - 3 bytes per pixel in the order R, G, B, the rows from top to bottom
 *   without gaps (the row length is imageWidth * 3 bytes)
 * - full range (0 - 255), BT.709 primaries, the transfer of SDR video as it is
 *   (BT.709, practically sRGB); HDR video is tone mapped to SDR with the sRGB
 *   transfer (AMBIENT_TONEMAPPED)
 * - it has the aspect ratio of the screen: imageWidth is the requested width,
 *   imageHeight follows the screen. The video is placed into it as it is placed
 *   on the screen (scaled video, letterbox or pillarbox of another aspect
 *   ratio), the rest is black.
 * - only the video, without OSD
 *
 * How the image is made is up to the output plugin: it may read it from the
 * displayed frame in the calling thread (drmdevice does so, the display thread
 * only hands over the buffer) or make it in its own render thread and only copy
 * it in the call. It should cost nothing, while nobody calls the service.
 *
 * The version is part of the name of the service. An incompatible change gets
 * a new name (e.g. "AmbientImage-v2.0"), new fields a new minor version; a
 * consumer may ask for the newer service with Data == NULL and fall back.
 *
 * This header is under the MIT license (unlike the rest of the plugin), so
 * every output plugin and every consumer can include it.
 *
 * @copyright 2026 by Andreas Baierl
 *
 * @license{MIT}
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __AMBIENTIMAGE_SERVICE_H
#define __AMBIENTIMAGE_SERVICE_H

#include <stdint.h>

#define AMBIENTIMAGE_SERVICE "AmbientImage-v1.0"

/** Flags of AmbientImage_v1_0_t::flags */
enum {
	AMBIENT_VIDEO      = 1 << 0,  ///< a video is displayed
	AMBIENT_NEW_IMAGE  = 1 << 1,  ///< rgb was filled with the image of a new frame
	AMBIENT_PAUSED     = 1 << 2,  ///< the playback is paused
	AMBIENT_DETACHED   = 1 << 3,  ///< the output device is detached (no video can be displayed)
	AMBIENT_TONEMAPPED = 1 << 4,  ///< the frame was HDR and is tone mapped to SDR
	AMBIENT_BUFFER_TOO_SMALL = 1 << 5, ///< rgbSize is too small, imageWidth and imageHeight tell the needed size
};

/**
 * Request of the ambient image service
 */
typedef struct {
	int structSize;           ///< in: sizeof(AmbientImage_v1_0_t)

	int width;                ///< in: width of the image (8 - 640), the height follows the aspect ratio of the screen
	int timeoutMs;            ///< in: wait up to this time for a frame newer than lastFrame (0: don't wait)
	unsigned int lastFrame;   ///< in: number of the last received frame (0: none)
	uint8_t *rgb;             ///< in: buffer for the image (see above)
	int rgbSize;              ///< in: size of the buffer (at least imageWidth * imageHeight * 3 bytes,
	                          ///<     width * width * 3 is enough for a screen wider than high)

	int flags;                ///< out: AMBIENT_* flags
	unsigned int frame;       ///< out: number of the frame (with AMBIENT_NEW_IMAGE)
	int imageWidth;           ///< out: width of the image (with AMBIENT_NEW_IMAGE or AMBIENT_BUFFER_TOO_SMALL)
	int imageHeight;          ///< out: height of the image (with AMBIENT_NEW_IMAGE or AMBIENT_BUFFER_TOO_SMALL)
	int sampleUs;             ///< out: time to read the image from the frame (microseconds)
	char format[80];          ///< out: description of the frame (e.g. "NV12/SAND128 1920x1080")
} AmbientImage_v1_0_t;

#endif
