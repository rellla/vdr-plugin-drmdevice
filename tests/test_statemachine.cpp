// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file test_statemachine.cpp
 * Unit tests for cStateMachine
 *
 * The state machine calls the device, which can't be built for the tests. So
 * statemachine.cpp is compiled here with a fake device, which records the calls.
 */

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "statemachine.h"

// the fake device and the VDR symbols aren't part of the documentation
/// @cond

/** Fake device, which records the calls of the state machine */
class cOutputDevice {
public:
	std::vector<std::string> calls;       ///< the calls in their order
	bool detachForced = false;            ///< result of IsDetachForced()
	int initAudioResult = 0;              ///< result of InitAudio()
	bool playbackStartScheduled = true;   ///< result of SchedulePlaybackStart()
	bool openGlThreadHalted = true;       ///< result of HaltOpenGlThread()

	void LeaveState(State state) { Call("Leave", StateToString(state)); }
	void EnterState(State state) { Call("Enter", StateToString(state)); }
	void HandleStillPicture(const StillPictureEvent &event) { Call("StillPicture", event.size); }
	void HandleDisplayModeChange(const sDrmMode &mode) { Call("DisplayMode", mode.width, mode.height); }
	void HaltVideoThreads(void) { Call("HaltVideo"); }
	void ResumeVideoThreads(void) { Call("ResumeVideo"); }
	bool HaltOpenGlThread(void) { Call("HaltOpenGl"); return openGlThreadHalted; }
	void ResumeOpenGlThread(void) { Call("ResumeOpenGl"); }
	bool IsDetachForced(void) { return detachForced; }
	void SetDetachForced(void) { Call("SetDetachForced"); }
	int InitAudio(bool) { Call("InitAudio"); return initAudioResult; }
	void ResetVideoFilter(void) { Call("ResetVideoFilter"); }
	void SetTrickSpeed(double speed, bool active, bool forward) { Call("TrickSpeed", speed, active, forward); }
	bool SchedulePlaybackStart(void) { Call("SchedulePlaybackStart"); return playbackStartScheduled; }
	void ScheduleResyncAtPtsMs(int64_t pts) { Call("ResyncAt", pts); }
	void ResumeFromPause(void) { Call("ResumeFromPause"); }
	void PausePlayback(bool pause) { Call("Pause", pause); }
	void ResumePlayback(void) { Call("Resume"); }

private:
	static std::string ToString(const char *value) { return value; }
	static std::string ToString(bool value) { return value ? "true" : "false"; }
	static std::string ToString(double value) { return std::to_string(static_cast<int>(value)); }
	template<typename T> static std::string ToString(T value) { return std::to_string(value); }

	template<typename... Args> void Call(const char *name, Args... args)
	{
		std::string call = name;
		((call += " " + ToString(args)), ...);
		calls.push_back(call);
	}
};

// compile the state machine with the fake device (including its OpenGL thread handling)
#define __DRMDEVICE_OUTPUT_DEVICE_H
#ifndef USE_GLES
#define USE_GLES
#endif
#include "statemachine.cpp"

// VDR symbols of the state machine and the event handler thread, which isn't started here
uint64_t cTimeMs::Now(void) { return 0; }
cMutex::cMutex(void) {}
cMutex::~cMutex() {}
cThread::cThread(const char *, bool) {}
cThread::~cThread() {}
bool cThread::Start(void) { return true; }
void cThread::Cancel(int) {}

/// @endcond

namespace {

using Calls = std::vector<std::string>;

/** The state machine in a given state with the fake device */
struct sFixture {
	cOutputDevice device;
	cStateMachine stateMachine{&device};

	explicit sFixture(State state) { stateMachine.SetState(state); }

	/** Send an event and return the calls of the device between the halt and the resume of the threads */
	Calls Send(const Event &event)
	{
		device.calls.clear();
		stateMachine.OnEventReceived(event);

		Calls actions;
		for (const auto &call : device.calls) {
			if (call != "HaltOpenGl" && call != "HaltVideo" && call != "ResumeVideo" && call != "ResumeOpenGl")
				actions.push_back(call);
		}
		return actions;
	}
};

const Event ALL_EVENTS[] = {
	PlayEvent{}, PauseEvent{}, StopEvent{}, TrickSpeedEvent{2.0, true, true}, StillPictureEvent{nullptr, 0},
	DetachEvent{}, AttachEvent{}, BufferUnderrunEvent{VIDEO}, BufferingThresholdReachedEvent{},
	ScheduleResyncAtPtsMsEvent{1000}, ResyncEvent{}, DisplayChangeEvent{},
};

} // namespace

TEST_CASE("State machine - state changes", "[statemachine]") {
	cOutputDevice device;
	cStateMachine stateMachine(&device);

	REQUIRE(stateMachine.GetState() == DETACHED);

	SECTION("the device leaves the old and enters the new state") {
		stateMachine.ChangeState(STOP);
		REQUIRE(stateMachine.GetState() == STOP);
		REQUIRE(device.calls == Calls{"Leave DETACHED", "Enter STOP"});
	}

	SECTION("the same state changes nothing") {
		stateMachine.ChangeState(DETACHED);
		REQUIRE(device.calls.empty());
	}

	SECTION("SetState() doesn't call the device") {
		stateMachine.SetState(PLAY);
		REQUIRE(stateMachine.GetState() == PLAY);
		REQUIRE(device.calls.empty());
	}
}

TEST_CASE("State machine - transitions", "[statemachine]") {
	// the state after each event of ALL_EVENTS
	struct sTransitions {
		State from;
		State to[std::size(ALL_EVENTS)];
	};
	//                Play         Pause        Stop         TrickSpeed   Still        Detach    Attach       Underrun     Threshold    ResyncAt     Resync       Display
	const sTransitions table[] = {
		{DETACHED,    {DETACHED,    DETACHED,    DETACHED,    DETACHED,    DETACHED,    DETACHED, STOP,        DETACHED,    DETACHED,    DETACHED,    DETACHED,    DETACHED}},
		{STOP,        {BUFFERING,   STOP,        STOP,        STOP,        STOP,        DETACHED, STOP,        STOP,        STOP,        STOP,        STOP,        STOP}},
		{BUFFERING,   {BUFFERING,   BUFFERING,   STOP,        TRICK_SPEED, BUFFERING,   DETACHED, BUFFERING,   BUFFERING,   PLAY,        PLAY,        BUFFERING,   BUFFERING}},
		{PLAY,        {PLAY,        PLAY,        STOP,        TRICK_SPEED, PLAY,        DETACHED, PLAY,        BUFFERING,   PLAY,        PLAY,        BUFFERING,   PLAY}},
		{TRICK_SPEED, {PLAY,        TRICK_SPEED, STOP,        TRICK_SPEED, TRICK_SPEED, DETACHED, TRICK_SPEED, TRICK_SPEED, TRICK_SPEED, TRICK_SPEED, TRICK_SPEED, TRICK_SPEED}},
	};

	for (const auto &row : table) {
		for (size_t i = 0; i < std::size(ALL_EVENTS); i++) {
			sFixture f(row.from);
			f.Send(ALL_EVENTS[i]);
			INFO(EventToString(ALL_EVENTS[i]) << " in " << StateToString(row.from));
			REQUIRE(StateToString(f.stateMachine.GetState()) == std::string(StateToString(row.to[i])));
		}
	}
}

TEST_CASE("State machine - events without an effect", "[statemachine]") {
	// invalid and ignored events don't call the device
	const std::pair<State, std::vector<size_t>> table[] = {
		{DETACHED,    {0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11}},
		{STOP,        {1, 2, 3, 6, 7, 8, 9, 10}},
		{BUFFERING,   {0, 1, 6, 7, 10}},
		{PLAY,        {6, 8}},
		{TRICK_SPEED, {6, 7, 8, 9, 10}},
	};

	for (const auto &[state, events] : table) {
		for (size_t i : events) {
			sFixture f(state);
			INFO(EventToString(ALL_EVENTS[i]) << " in " << StateToString(state));
			REQUIRE(f.Send(ALL_EVENTS[i]).empty());
			REQUIRE(f.stateMachine.GetState() == state);
		}
	}
}

TEST_CASE("State machine - DETACHED", "[statemachine]") {
	sFixture f(DETACHED);

	SECTION("attach") {
		REQUIRE(f.Send(AttachEvent{}) == Calls{"Leave DETACHED", "Enter STOP"});
	}

	SECTION("a forced detach stays detached") {
		f.device.detachForced = true;
		REQUIRE(f.Send(AttachEvent{}).empty());
		REQUIRE(f.stateMachine.GetState() == DETACHED);
	}

	SECTION("no thread is halted or resumed, also not after the attach") {
		f.stateMachine.OnEventReceived(PlayEvent{});
		f.stateMachine.OnEventReceived(AttachEvent{});
		REQUIRE(f.device.calls == Calls{"Leave DETACHED", "Enter STOP"});
	}
}

TEST_CASE("State machine - STOP", "[statemachine]") {
	sFixture f(STOP);

	SECTION("play starts the buffering") {
		REQUIRE(f.Send(PlayEvent{}) == Calls{"InitAudio", "Leave STOP", "Enter BUFFERING", "ResetVideoFilter"});
	}

	SECTION("play without audio device detaches") {
		f.device.initAudioResult = -1;
		f.stateMachine.OnEventReceived(PlayEvent{});
		REQUIRE(f.stateMachine.GetState() == DETACHED);
		// the video threads aren't resumed in the detached state
		REQUIRE(f.device.calls == Calls{"HaltOpenGl", "HaltVideo", "InitAudio", "SetDetachForced",
		                                "Leave STOP", "Enter DETACHED", "ResumeOpenGl"});
	}

	SECTION("still picture and display mode change") {
		REQUIRE(f.Send(StillPictureEvent{nullptr, 188}) == Calls{"StillPicture 188"});
		REQUIRE(f.Send(DisplayChangeEvent{{1920, 1080, 50.0, false}}) == Calls{"DisplayMode 1920 1080"});
		REQUIRE(f.stateMachine.GetState() == STOP);
	}
}

TEST_CASE("State machine - BUFFERING", "[statemachine]") {
	sFixture f(BUFFERING);

	SECTION("the playback starts, when enough data is buffered") {
		REQUIRE(f.Send(BufferingThresholdReachedEvent{}) == Calls{"SchedulePlaybackStart", "Leave BUFFERING", "Enter PLAY"});
	}

	SECTION("the buffering goes on, if the playback start can't be scheduled") {
		f.device.playbackStartScheduled = false;
		REQUIRE(f.Send(BufferingThresholdReachedEvent{}) == Calls{"SchedulePlaybackStart"});
		REQUIRE(f.stateMachine.GetState() == BUFFERING);
	}

	SECTION("trick speed aborts the buffering") {
		REQUIRE(f.Send(TrickSpeedEvent{4.0, true, false}) == Calls{"Leave BUFFERING", "Enter PLAY", "TrickSpeed 4 true false",
		                                                           "Leave PLAY", "Enter TRICK_SPEED"});
	}

	SECTION("a scheduled resync starts the playback") {
		REQUIRE(f.Send(ScheduleResyncAtPtsMsEvent{5000}) == Calls{"Leave BUFFERING", "Enter PLAY", "ResyncAt 5000"});
	}

	SECTION("stop, still picture and display mode change") {
		REQUIRE(f.Send(StillPictureEvent{nullptr, 376}) == Calls{"StillPicture 376"});
		REQUIRE(f.Send(DisplayChangeEvent{{1280, 720, 50.0, false}}) == Calls{"DisplayMode 1280 720"});
		REQUIRE(f.Send(StopEvent{}) == Calls{"Leave BUFFERING", "Enter STOP"});
	}
}

TEST_CASE("State machine - PLAY", "[statemachine]") {
	sFixture f(PLAY);

	SECTION("pause and play") {
		REQUIRE(f.Send(PauseEvent{}) == Calls{"Pause true"});
		REQUIRE(f.Send(PlayEvent{}) == Calls{"ResumeFromPause"});
		REQUIRE(f.stateMachine.GetState() == PLAY);
	}

	SECTION("trick speed") {
		REQUIRE(f.Send(TrickSpeedEvent{8.0, true, true}) == Calls{"TrickSpeed 8 true true", "Leave PLAY", "Enter TRICK_SPEED"});
	}

	SECTION("a buffer underrun and a resync buffer again") {
		REQUIRE(f.Send(BufferUnderrunEvent{AUDIO}) == Calls{"Leave PLAY", "Enter BUFFERING"});
		f.stateMachine.SetState(PLAY);
		REQUIRE(f.Send(BufferUnderrunEvent{VIDEO}) == Calls{"Leave PLAY", "Enter BUFFERING"});
		f.stateMachine.SetState(PLAY);
		REQUIRE(f.Send(ResyncEvent{}) == Calls{"Leave PLAY", "Enter BUFFERING"});
	}

	SECTION("a scheduled resync, still picture and display mode change keep the state") {
		REQUIRE(f.Send(ScheduleResyncAtPtsMsEvent{90000}) == Calls{"ResyncAt 90000"});
		REQUIRE(f.Send(StillPictureEvent{nullptr, 564}) == Calls{"StillPicture 564"});
		REQUIRE(f.Send(DisplayChangeEvent{{3840, 2160, 60.0, false}}) == Calls{"DisplayMode 3840 2160"});
		REQUIRE(f.stateMachine.GetState() == PLAY);
	}

	SECTION("the threads are halted during the event and resumed afterwards") {
		f.stateMachine.OnEventReceived(StopEvent{});
		REQUIRE(f.device.calls == Calls{"HaltOpenGl", "HaltVideo", "Leave PLAY", "Enter STOP", "ResumeVideo", "ResumeOpenGl"});
	}

	SECTION("the OpenGL thread is only resumed, if it was halted") {
		f.device.openGlThreadHalted = false;
		f.stateMachine.OnEventReceived(StopEvent{});
		REQUIRE(f.device.calls == Calls{"HaltOpenGl", "HaltVideo", "Leave PLAY", "Enter STOP", "ResumeVideo"});
	}

	SECTION("the video threads aren't resumed after a detach") {
		f.stateMachine.OnEventReceived(DetachEvent{});
		REQUIRE(f.device.calls == Calls{"HaltOpenGl", "HaltVideo", "Leave PLAY", "Enter DETACHED", "ResumeOpenGl"});
	}
}

TEST_CASE("State machine - TRICK_SPEED", "[statemachine]") {
	sFixture f(TRICK_SPEED);

	SECTION("play ends the trick speed") {
		REQUIRE(f.Send(PlayEvent{}) == Calls{"Leave TRICK_SPEED", "Enter PLAY"});
	}

	SECTION("pause stops the trick speed, but keeps the state") {
		REQUIRE(f.Send(PauseEvent{}) == Calls{"TrickSpeed 0 false false", "Pause false"});
		REQUIRE(f.stateMachine.GetState() == TRICK_SPEED);
	}

	SECTION("another speed or direction resumes the playback") {
		REQUIRE(f.Send(TrickSpeedEvent{2.0, true, false}) == Calls{"TrickSpeed 2 true false", "Resume"});
	}

	SECTION("still picture and display mode change") {
		REQUIRE(f.Send(StillPictureEvent{nullptr, 752}) == Calls{"StillPicture 752"});
		REQUIRE(f.Send(DisplayChangeEvent{{720, 576, 50.0, true}}) == Calls{"DisplayMode 720 576"});
	}
}
