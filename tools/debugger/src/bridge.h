// Runs Ikemen GO as a child process with piped stdin/stdout/stderr.
#pragma once

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class Bridge {
public:
    struct Line {
        bool err; // from stderr (Ikemen's log) instead of stdout
        std::string text;
    };

    ~Bridge() { Stop(); }

    // exe is Ikemen_GO.exe; it runs in its own folder.
    bool Launch(const std::string &exe, const std::string &args, std::string &error, bool hidden = false);
    // Ends the game if it still runs.
    void Stop();
    bool Running() const;
    DWORD Pid() const { return pid_; }
    DWORD ExitCode() const;
    // Writes one line to the game's stdin; the game runs it as Lua on its next frame.
    bool Send(const std::string &line);
    // Moves the lines read since the last call into out.
    void Poll(std::vector<Line> &out);

private:
    void Reader(HANDLE pipe, bool err);

    HANDLE process_ = nullptr;
    HANDLE stdin_ = nullptr;
    DWORD pid_ = 0;
    std::thread out_, err_;
    std::mutex mutex_;
    std::vector<Line> queue_;
};

// A Lua string literal holding s.
std::string LuaQuote(const std::string &s);
