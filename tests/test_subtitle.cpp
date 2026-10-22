/**
 * @file test_subtitle.cpp
 * Unit tests for the text conversion of the mediaplayer subtitles
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "../subtitle.h"

using Lines = std::vector<std::string>;

TEST_CASE("Subtitles - ASS dialogue", "[subtitle]") {
	SECTION("the text starts after the 8th field") {
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,Hello world") == Lines{ "Hello world" });
	}

	SECTION("commas in the text are kept") {
		REQUIRE(AssDialogueToLines("1,0,Default,,0,0,0,,Yes, sir.") == Lines{ "Yes, sir." });
	}

	SECTION("line breaks") {
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,First\\NSecond") == Lines{ "First", "Second" });
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,First\\nSecond") == Lines{ "First", "Second" });
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,First\\N\\NSecond") == Lines{ "First", "Second" });
	}

	SECTION("override codes are removed") {
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,{\\i1}italic{\\i0} text") == Lines{ "italic text" });
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,{\\an8}{\\pos(10,10)}Top") == Lines{ "Top" });
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,broken {\\i1") == Lines{ "broken" });
	}

	SECTION("hard spaces") {
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,a\\hb") == Lines{ "a b" });
	}

	SECTION("the old format with the Dialogue prefix") {
		REQUIRE(AssDialogueToLines("Dialogue: 0,0:00:01.00,0:00:02.00,Default,,0,0,0,,Old style") == Lines{ "Old style" });
	}

	SECTION("no text") {
		REQUIRE(AssDialogueToLines("0,0,Default,,0,0,0,,").empty());
		REQUIRE(AssDialogueToLines("0,0,Default").empty());
		REQUIRE(AssDialogueToLines(nullptr).empty());
	}
}

TEST_CASE("Subtitles - plain text", "[subtitle]") {
	REQUIRE(TextToLines("One\nTwo\r\n") == Lines{ "One", "Two" });
	REQUIRE(TextToLines("<i>italic</i> and <font color=\"red\">red</font>") == Lines{ "italic and red" });
	REQUIRE(TextToLines("a < b") == Lines{ "a < b" });
	REQUIRE(TextToLines("  \n ").empty());
	REQUIRE(TextToLines(nullptr).empty());
}
