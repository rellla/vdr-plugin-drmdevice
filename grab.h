// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file grab.h
 * Grabbing Interface Header File
 *
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_GRAB_H
#define __DRMDEVICE_GRAB_H

#include <atomic>
#include <cstdint>

#include <vdr/osd.h>

class cDrmBuffer;
class cVideoRender;

/**
 * Grabbing Buffer
 *
 * Holds the data for a grabbed buffer.
 * The grab is triggered by VDR/ cOutputDevice, data is set by the renderer
 * and composed by cOutputDevice again.
 *
 * @ingroup misc
 */
class cGrabBuffer {
public:
	/** Create an empty grab buffer, the identifier is used for logging */
	cGrabBuffer(const char *identifier) : m_identifier(identifier) {};

	void FreeInput(void);
	void Clear(void);
	void Set(cDrmBuffer *);
	bool IsSet(void) { return !m_outputRect.IsEmpty(); }  ///< true, if a buffer was grabbed
	uint8_t *ConvertToRgb(int *);

	// setters and getters
	void SetOutputData(uint8_t *result) { m_pOutputData = result; } ///< set the converted image
	uint8_t *GetOutputData(void) { return m_pOutputData; }          ///< get the converted image
	void SetOutputSize(int size) { m_outputSize = size; }           ///< set the size of the converted image
	int GetOutputSize(void) { return m_outputSize; }                ///< get the size of the converted image
	int GetOutputX(void) { return m_outputRect.X(); }               ///< x position of the grabbed buffer on screen
	int GetOutputY(void) { return m_outputRect.Y(); }               ///< y position of the grabbed buffer on screen
	int GetOutputWidth(void) { return m_outputRect.Width(); }       ///< width of the grabbed buffer on screen
	int GetOutputHeight(void) { return m_outputRect.Height(); }     ///< height of the grabbed buffer on screen

private:
	// output
	uint8_t *m_pOutputData = nullptr;    ///< pointer to grabbed image
	int m_outputSize = 0;                ///< size of grabbed data
	cRect m_outputRect;                  ///< rect of the grabbed data
	cRect m_cropRect;                    ///< shown part of the grabbed data (empty: all)

	// input (copied from original cDrmBuffer)
	uint32_t m_width = 0;                               ///< buffer width
	uint32_t m_height = 0;                              ///< buffer height
	uint32_t m_pixFmt = 0;                              ///< drm pixel format
	uint64_t m_modifier = 0;                            ///< format modifier
	int m_numPlanes = 0;                                ///< number of planes
	uint8_t *m_pPlane[4] = {};                          ///< copied data of the planes
	uint32_t m_offset[4] = {};                          ///< offsets of the planes
	uint32_t m_pitch[4] = {};                           ///< pitches of the planes
	uint32_t m_size[4] = {};                            ///< sizes of the planes

	const char *m_identifier;                           ///< identifier string for logging
};

/**
 * Grabbing Processor
 *
 * Handles the grabbing workflow from triggering the grab to returning the result
 *
 * @ingroup misc
 */
class cGrabber {
public:
	/** Create a grabber for the given renderer */
	cGrabber(cVideoRender *render) : m_pRender(render) {};

	bool IsActive(void) { return m_active; }            ///< true, if a grab is running
	bool Start(bool, int, int, int, int, int);
	uint8_t *Image(void) { return m_grabbedImage; }     ///< get the grabbed image (jpeg or pnm)
	int Size(void) { return m_grabbedSize; }            ///< get the size of the grabbed image
	bool ProcessGrab(void);
	void Finish(void);

private:
	cVideoRender *m_pRender;         ///< pointer to cVideoRender object
	uint8_t *m_grabbedImage = nullptr; ///< pointer to the finished grabbed image
	int m_grabbedSize = 0;           ///< data size of the grabbed image
	std::atomic<bool> m_active = false; ///< true, if a grab process is currently running

	bool m_isJpeg = true;            ///< true, if a jpeg image was requested
	int m_quality = 0;               ///< quality of the jpeg image
	int m_grabbedWidth = 0;          ///< pixel width of the grabbed image
	int m_grabbedHeight = 0;         ///< pixel height of the grabbed image
	int m_screenWidth = 0;           ///< pixel screenwidth
	int m_screenHeight = 0;          ///< pixel screenheight

	uint8_t *GetGrabbedVideoData(int *, int *, int *, int *, int *);
	uint8_t *GetGrabbedPipData(int *, int *, int *, int *, int *);
	uint8_t *GetGrabbedOsdData(int *, int *, int *, int *, int *);
	uint8_t *GetGrabbedData(int *, int *, int *, int *, int *, cGrabBuffer *);
};

#endif
