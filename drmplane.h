// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file drmplane.h
 * DRM Plane Header File
 *
 * @copyright 2018 by zille.  All Rights Reserved.
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_DRMPLANE_H
#define __DRMDEVICE_DRMPLANE_H

#include <xf86drmMode.h>

/**
 * DRM Plane
 *
 * Holds the id, type and properties of a drm plane and caches the modesetting
 * parameters (crtc, framebuffer, source and destination rectangle), which are
 * added to an atomic modeset request by SetPlane().
 *
 * @ingroup drm
 */
class cDrmPlane {
public:
	cDrmPlane(void) = default;

	void SetParams(uint64_t, uint64_t,
	               uint64_t, uint64_t, uint64_t, uint64_t,
	               uint64_t, uint64_t, uint64_t, uint64_t);
	void DumpParameters(const char *);
	void FillProperties(int);
	void FreeProperties(void);
	void SetPlaneZpos(drmModeAtomicReqPtr);
	void SetPlane(drmModeAtomicReqPtr);
	void ClearPlane(drmModeAtomicReqPtr);
	int HasZpos(int);

	// getters and setters
	uint32_t GetId(void) { return m_planeId; }          ///< plane id (0, if not set)
	void SetId(uint32_t id) { m_planeId = id; };
	uint64_t GetType(void) { return m_type; }           ///< plane type (DRM_PLANE_TYPE_*)
	void SetType(uint64_t type) { m_type = type; };
	uint64_t GetCrtcId(void) { return m_crtcId; };
	uint64_t GetFbId(void) { return m_fbId; };
	uint64_t GetCrtcX(void) { return m_crtcX; };
	uint64_t GetCrtcY(void) { return m_crtcY; };
	uint64_t GetCrtcW(void) { return m_crtcW; };
	uint64_t GetCrtcH(void) { return m_crtcH; };
	uint64_t GetSrcX(void) { return m_srcX; };
	uint64_t GetSrcY(void) { return m_srcY; };
	uint64_t GetSrcW(void) { return m_srcW; };
	uint64_t GetSrcH(void) { return m_srcH; };
	uint64_t GetZpos(void) { return m_zpos; }           ///< zpos of the plane
	void SetZpos(uint64_t zpos) { m_zpos = zpos; };

	int GetCountProps(void) { return m_props ? m_props->count_props : 0; }       ///< number of properties
	char *GetPropsInfoName(int prop) { return m_propsInfo[prop]->name; }         ///< name of the given property
	uint32_t GetPropsInfoPropId(int prop) { return m_propsInfo[prop]->prop_id; } ///< id of the given property
	drmModeObjectProperties *GetProps(void) { return m_props; };
	void SetProps(drmModeObjectProperties *props) { m_props = props; };
	drmModePropertyRes **GetPropsInfo(void) { return m_propsInfo; };
	drmModePropertyRes **GetPropsInfoElem(int elem) { return &m_propsInfo[elem]; };

private:
	uint32_t m_planeId = 0;             ///< the plane's ID
	uint64_t m_type = 0;                ///< type: DRM_PLANE_TYPE_PRIMARY or
	                                    ///<       DRM_PLANE_TYPE_OVERLAY
	drmModeObjectProperties *m_props = nullptr;         ///< properties of the plane
	drmModePropertyRes **m_propsInfo = nullptr;         ///< property infos (names, ids) of the plane

	// The modesetting parameters for a drm commit
	uint64_t m_crtcId = 0;            ///< CRTC_ID
	uint64_t m_fbId = 0;              ///< FB_ID
	uint64_t m_crtcX = 0;             ///< CRTC_X
	uint64_t m_crtcY = 0;             ///< CRTC_Y
	uint64_t m_crtcW = 0;             ///< CRTC_W
	uint64_t m_crtcH = 0;             ///< CRTC_H
	uint64_t m_srcX = 0;              ///< SRC_X
	uint64_t m_srcY = 0;              ///< SRC_Y
	uint64_t m_srcW = 0;              ///< SRC_W
	uint64_t m_srcH = 0;              ///< SRC_H
	uint64_t m_zpos = 0;              ///< ZPOS

	int SetPropertyRequest(drmModeAtomicReqPtr, const char *, uint64_t);
};

#endif
