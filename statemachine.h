// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file statemachine.h
 * Device State Machine and Event Handler Header File
 *
 * @copyright 2025 - 2026 by Andreas Baierl. All Rights Reserved.
 *
 * @license{AGPL-3.0-or-later}
 */

#ifndef __DRMDEVICE_STATEMACHINE_H
#define __DRMDEVICE_STATEMACHINE_H

#if __cplusplus < 201703L
#error "C++17 or higher is required"
#endif

#include <atomic>
#include <mutex>
#include <variant>
#include <vector>

#include <vdr/thread.h>
#include <vdr/tools.h>

#include "config.h"
#include "logger.h"

/**
 * @addtogroup device
 * @{
 */

/**
 * Helper to visit a std::variant with a set of lambdas (C++17 visitor pattern)
 *
 * Each lambda handles one alternative of the variant, e.g.
 * @code
 * std::visit(overload{
 *     [](const PlayEvent&) { ... },
 *     [](const StopEvent&) { ... },
 * }, event);
 * @endcode
 */
template<class... Ts>
struct overload : Ts... { using Ts::operator()...; };
/** Deduction guide for overload */
template<class... Ts> overload(Ts...) -> overload<Ts...>;

/**
 * States of the device
 */
enum State {
	STOP,          ///< no playback (VDR's play mode pmNone)
	BUFFERING,     ///< playback was requested, the buffers are filled before the playback starts
	PLAY,          ///< normal playback (or paused)
	TRICK_SPEED,   ///< fast/slow forward/backward playback
	DETACHED       ///< the device has released the hardware (DRM, ALSA)
};

/**
 * Stream, whose buffer ran empty
 */
enum BufferUnderrunType {
	VIDEO,         ///< the video buffer ran empty
	AUDIO,         ///< the audio buffer ran empty
};

/** Start the playback (VDR's Play() or a new play mode) */
struct PlayEvent {};
/** Pause the playback (VDR's Freeze()) */
struct PauseEvent {};
/** Stop the playback (play mode pmNone) */
struct StopEvent {};
/** Start, change or stop trick speed (VDR's TrickSpeed()) */
struct TrickSpeedEvent {
	double speed;          ///< normalized speed factor
	bool active;           ///< true, if trick speed is active (speed != 0)
	bool forward;          ///< true, if playing forward
};
/** Display a still picture (VDR's StillPicture()) */
struct StillPictureEvent {
	const uchar *data;     ///< pes data of one or more frames (owned by VDR)
	int size;              ///< size of the data
};
/** Release the hardware (detach or an external player takes over) */
struct DetachEvent {};
/** Acquire the hardware again */
struct AttachEvent {};
/** The audio or video buffer ran empty */
struct BufferUnderrunEvent {
	BufferUnderrunType type;  ///< the stream, whose buffer ran empty
};
/** Enough data is buffered, the playback can start */
struct BufferingThresholdReachedEvent {};
/** Resync audio and video, as soon as the video reaches the given pts */
struct ScheduleResyncAtPtsMsEvent {
	int64_t pts;           ///< pts in ms, at which the resync shall be done
};
/** Resync audio and video now */
struct ResyncEvent {};
/** Change the display mode (resolution, refresh rate) */
struct DisplayChangeEvent {
	sDrmMode mode;         ///< the new display mode
};

/**
 * All events, which can be sent to the state machine
 */
using Event = std::variant<
	PlayEvent,
	PauseEvent,
	StopEvent,
	TrickSpeedEvent,
	StillPictureEvent,
	DetachEvent,
	AttachEvent,
	BufferUnderrunEvent,
	BufferingThresholdReachedEvent,
	ScheduleResyncAtPtsMsEvent,
	ResyncEvent,
	DisplayChangeEvent
>;

/**
 * Get the name of an event (for logging)
 *
 * @param e      event
 * @return       name of the event
 */
inline const char* EventToString(const Event& e) {
	return std::visit(overload{
		[](const PlayEvent&) -> const char* { return "PlayEvent"; },
		[](const PauseEvent&) -> const char* { return "PauseEvent"; },
		[](const StopEvent&) -> const char* { return "StopEvent"; },
		[](const TrickSpeedEvent&) -> const char* { return "TrickSpeedEvent"; },
		[](const StillPictureEvent&) -> const char* { return "StillPictureEvent"; },
		[](const DetachEvent&) -> const char* { return "DetachEvent"; },
		[](const AttachEvent&) -> const char* { return "AttachEvent"; },
		[](const BufferUnderrunEvent& e) -> const char* { return e.type == AUDIO ? "BufferUnderrunEvent: Audio" : "BufferUnderrunEvent: Video"; },
		[](const BufferingThresholdReachedEvent&) -> const char* { return "BufferingThresholdReachedEvent"; },
		[](const ScheduleResyncAtPtsMsEvent&) -> const char* { return "ScheduleResyncAtPtsMsEvent"; },
		[](const ResyncEvent&) -> const char* { return "ResyncEvent"; },
		[](const DisplayChangeEvent&) -> const char* { return "DisplayChangeEvent"; },
	}, e);
}

/**
 * Get the name of a state (for logging)
 *
 * @param s      state
 * @return       name of the state
 */
inline const char* StateToString(State s) {
	switch(s) {
		case State::STOP: return "STOP";
		case State::BUFFERING: return "BUFFERING";
		case State::PLAY: return "PLAY";
		case State::TRICK_SPEED: return "TRICK_SPEED";
		case State::DETACHED: return "DETACHED";
	}
	return "Unknown";
}

/** @} */

class cOutputDevice;

/**
 * State Machine Implementation
 *
 * Events can be pushed directly to the state machine (OnEventReceived())
 * or can be pushed through the event handler queue.
 * Pushing directly may block the calling thread until OnEventReceiced()
 * returns. Pushing the event to the event handler queue avoids this locking
 * but does not guarantee, that the event is processed immediately.
 *
 * The user may choose, which solution fits best for the present use case.
 *
 *
 * @ingroup device
 */
class cStateMachine {
public:
	cStateMachine(cOutputDevice *);
	void OnEventReceived(const Event&);

	State GetState(void) const { return m_state; }           ///< get the current state
	void SetState(State state) { m_state = state; }          ///< set the state without leaving/entering a state
	void ChangeState(State);
	/** Lock the state machine, the returned lock unlocks it, when it goes out of scope */
	std::unique_lock<std::mutex> Lock(void) const { return std::unique_lock<std::mutex>(m_mutex); };
	std::unique_lock<std::mutex> TryLock(void) const { return std::unique_lock<std::mutex>(m_mutex, std::try_to_lock); } ///< like Lock(), but don't wait (check owns_lock())

private:
	cOutputDevice *m_pDevice;                ///< pointer to the device
	mutable std::mutex m_mutex;              ///< state machine mutex
	std::atomic<State> m_state = DETACHED;   ///< current state
};

/**
 * Event handler thread
 *
 * Queues events and sends them to the state machine cStateMachine as the final event receiver
 *
 * @ingroup device
 */
class cEventHandler : public cThread {
public:
	cEventHandler(cStateMachine *);
	~cEventHandler(void);
	void AddEvent(Event);

protected:
	void Action(void);

private:
	cStateMachine *m_pStateMachine;   ///< pointer to state machine
	std::mutex m_mutex;               ///< queue mutex
	std::vector<Event> m_eventQueue;  ///< event fifo queue
};

#endif
