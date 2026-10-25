#include "bridge.h"

static std::wstring Widen(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

bool Bridge::Launch(const std::string &exe, const std::string &args, std::string &error) {
    Stop();

    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE inRead, inWrite, outRead, outWrite, errRead, errWrite;
    if (!CreatePipe(&inRead, &inWrite, &sa, 0)) {
        error = "CreatePipe failed";
        return false;
    }
    CreatePipe(&outRead, &outWrite, &sa, 0);
    CreatePipe(&errRead, &errWrite, &sa, 0);
    // Our ends must not leak into the child, or the readers never see the end of the pipe.
    SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errRead, HANDLE_FLAG_INHERIT, 0);

    std::wstring wexe = Widen(exe);
    std::wstring dir = wexe;
    size_t slash = dir.find_last_of(L"\\/");
    dir = slash == std::wstring::npos ? L"." : dir.substr(0, slash);
    std::wstring cmd = L"\"" + wexe + L"\" " + Widen(args);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = inRead;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessW(wexe.c_str(), &cmd[0], nullptr, nullptr, TRUE, 0, nullptr, dir.c_str(), &si, &pi);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (!ok) {
        error = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        CloseHandle(inWrite);
        CloseHandle(outRead);
        CloseHandle(errRead);
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    pid_ = pi.dwProcessId;
    stdin_ = inWrite;
    out_ = std::thread(&Bridge::Reader, this, outRead, false);
    err_ = std::thread(&Bridge::Reader, this, errRead, true);
    return true;
}

void Bridge::Stop() {
    if (process_) {
        if (Running()) TerminateProcess(process_, 1);
        WaitForSingleObject(process_, 2000);
    }
    if (stdin_) CloseHandle(stdin_);
    stdin_ = nullptr;
    if (out_.joinable()) out_.join();
    if (err_.joinable()) err_.join();
    if (process_) CloseHandle(process_);
    process_ = nullptr;
}

bool Bridge::Running() const {
    return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

DWORD Bridge::ExitCode() const {
    DWORD code = 0;
    if (process_) GetExitCodeProcess(process_, &code);
    return code;
}

bool Bridge::Send(const std::string &line) {
    if (!stdin_ || !Running()) return false;
    std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(stdin_, data.data(), (DWORD)data.size(), &written, nullptr) && written == data.size();
}

void Bridge::Poll(std::vector<Line> &out) {
    std::lock_guard<std::mutex> lock(mutex_);
    out.insert(out.end(), queue_.begin(), queue_.end());
    queue_.clear();
}

void Bridge::Reader(HANDLE pipe, bool err) {
    std::string pending;
    char buf[8192];
    DWORD got = 0;
    while (ReadFile(pipe, buf, sizeof(buf), &got, nullptr) && got > 0) {
        pending.append(buf, got);
        size_t start = 0, nl;
        std::vector<Line> lines;
        while ((nl = pending.find('\n', start)) != std::string::npos) {
            size_t end = nl > start && pending[nl - 1] == '\r' ? nl - 1 : nl;
            lines.push_back({err, pending.substr(start, end - start)});
            start = nl + 1;
        }
        pending.erase(0, start);
        if (!lines.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.insert(queue_.end(), lines.begin(), lines.end());
        }
    }
    if (!pending.empty()) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push_back({err, pending});
    }
    CloseHandle(pipe);
}

std::string LuaQuote(const std::string &s) {
    std::string q = "'";
    for (char c : s) {
        switch (c) {
        case '\\': q += "\\\\"; break;
        case '\'': q += "\\'"; break;
        case '\n': q += "\\n"; break;
        case '\r': q += "\\r"; break;
        case '\0': q += "\\0"; break;
        default: q += c; break;
        }
    }
    return q + "'";
}
