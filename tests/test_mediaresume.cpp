/**
 * @file test_mediaresume.cpp
 * Unit tests for cMediaResume
 *
 * @license{AGPL-3.0-or-later}
 */

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

#include "../mediaresume.h"

/**
 * Temporary resume file, which is removed at the end of a test
 */
class cTmpResumeFile {
public:
	cTmpResumeFile(void) : m_name("test_mediaresume_" + std::to_string(getpid()) + ".txt") { std::remove(m_name.c_str()); }
	~cTmpResumeFile() { std::remove(m_name.c_str()); }
	const std::string &Name(void) const { return m_name; }
	std::string Content(void) const {
		std::ifstream file(m_name);
		return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
private:
	std::string m_name;
};

TEST_CASE("cMediaResume - Missing file", "[mediaresume]") {
	cTmpResumeFile tmp;
	cMediaResume resume(tmp.Name());

	REQUIRE(resume.Get("/video/movie.mkv") == -1);
	REQUIRE(resume.Delete("/video/movie.mkv"));
}

TEST_CASE("cMediaResume - Store and get", "[mediaresume]") {
	cTmpResumeFile tmp;
	cMediaResume resume(tmp.Name());

	REQUIRE(resume.Store("/video/movie.mkv", 123456));
	REQUIRE(resume.Store("/video/with space.mp4", 42000));

	REQUIRE(resume.Get("/video/movie.mkv") == 123456);
	REQUIRE(resume.Get("/video/with space.mp4") == 42000);
	REQUIRE(resume.Get("/video/other.mkv") == -1);

	SECTION("A new instance reads the stored positions") {
		cMediaResume resume2(tmp.Name());
		REQUIRE(resume2.Get("/video/movie.mkv") == 123456);
	}

	SECTION("Storing again replaces the position and moves it to the top") {
		REQUIRE(resume.Store("/video/movie.mkv", 7000));
		REQUIRE(resume.Get("/video/movie.mkv") == 7000);
		REQUIRE(tmp.Content() == "7000 /video/movie.mkv\n42000 /video/with space.mp4\n");
	}

	SECTION("Delete removes only the given file") {
		REQUIRE(resume.Delete("/video/movie.mkv"));
		REQUIRE(resume.Get("/video/movie.mkv") == -1);
		REQUIRE(resume.Get("/video/with space.mp4") == 42000);
	}

	SECTION("A position <= 0 deletes the position") {
		REQUIRE(resume.Store("/video/movie.mkv", 0));
		REQUIRE(resume.Get("/video/movie.mkv") == -1);
		REQUIRE(resume.Store("/video/with space.mp4", -5000));
		REQUIRE(resume.Get("/video/with space.mp4") == -1);
	}
}

TEST_CASE("cMediaResume - Invalid paths are not stored", "[mediaresume]") {
	cTmpResumeFile tmp;
	cMediaResume resume(tmp.Name());

	REQUIRE_FALSE(resume.Store("", 1000));
	REQUIRE_FALSE(resume.Store("/video/new\nline.mkv", 1000));
	REQUIRE(resume.Get("/video/new") == -1);
}

TEST_CASE("cMediaResume - Only the newest entries are kept", "[mediaresume]") {
	cTmpResumeFile tmp;
	cMediaResume resume(tmp.Name(), 3);

	for (int i = 1; i <= 5; i++)
		REQUIRE(resume.Store("/video/" + std::to_string(i) + ".mkv", i * 1000));

	REQUIRE(resume.Get("/video/1.mkv") == -1);
	REQUIRE(resume.Get("/video/2.mkv") == -1);
	REQUIRE(resume.Get("/video/3.mkv") == 3000);
	REQUIRE(resume.Get("/video/4.mkv") == 4000);
	REQUIRE(resume.Get("/video/5.mkv") == 5000);
}

TEST_CASE("cMediaResume - Invalid lines are skipped", "[mediaresume]") {
	cTmpResumeFile tmp;
	{
		std::ofstream file(tmp.Name());
		file << "garbage\n"
		     << "12x /video/a.mkv\n"
		     << " /video/b.mkv\n"
		     << "-100 /video/c.mkv\n"
		     << "5000 \n"
		     << "\n"
		     << "6000 /video/ok.mkv\n";
	}
	cMediaResume resume(tmp.Name());

	REQUIRE(resume.Get("/video/a.mkv") == -1);
	REQUIRE(resume.Get("/video/b.mkv") == -1);
	REQUIRE(resume.Get("/video/c.mkv") == -1);
	REQUIRE(resume.Get("/video/ok.mkv") == 6000);

	// rewriting the file drops the invalid lines
	REQUIRE(resume.Store("/video/new.mkv", 1000));
	REQUIRE(tmp.Content() == "1000 /video/new.mkv\n6000 /video/ok.mkv\n");
}
