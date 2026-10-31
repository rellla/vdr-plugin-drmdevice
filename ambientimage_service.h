// SPDX-License-Identifier: AGPL-3.0-or-later

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
 *   uint8_t rgb[160 * 90 * 3];
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
 * and reads the image from it in the calling thread. While nobody calls the
 * service, the output plugin doesn't take any snapshots, so the service costs
 * nothing, if it isn't used.
 *
 * A call with Data == NULL returns true, if the service is provided.
 *
 * @copyright 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
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
};

/**
 * Request of the ambient image service
 */
typedef struct {
	int structSize;           ///< in: sizeof(AmbientImage_v1_0_t)

	int width;                ///< in: width of the image (8 - 640), the height follows the aspect ratio of the screen
	int timeoutMs;            ///< in: wait up to this time for a frame newer than lastFrame (0: don't wait)
	unsigned int lastFrame;   ///< in: number of the last received frame (0: none)
	uint8_t *rgb;             ///< in: buffer for the image (RGB24, width * height * 3 bytes)
	int rgbSize;              ///< in: size of the buffer

	int flags;                ///< out: AMBIENT_* flags
	unsigned int frame;       ///< out: number of the frame (with AMBIENT_NEW_IMAGE)
	int imageWidth;           ///< out: width of the image (with AMBIENT_NEW_IMAGE)
	int imageHeight;          ///< out: height of the image (with AMBIENT_NEW_IMAGE)
	int sampleUs;             ///< out: time to read the image from the frame (microseconds)
	char format[80];          ///< out: description of the frame (e.g. "NV12/SAND128 1920x1080")
} AmbientImage_v1_0_t;

#endif
