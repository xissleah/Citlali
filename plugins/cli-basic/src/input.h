#pragma once
#include <atomic>
#include <string>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif
namespace cli {
// Poll in this frontend only: core neither reads prompts nor drives runtime generation.
class Input {
    std::string bytes;
#ifdef _WIN32
    HANDLE handle = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring console_line;
    static std::string utf8(const std::wstring &s) {
        int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr,
                                    nullptr);
        std::string out(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                            nullptr);
        return out;
    }
#endif
  public:
    // 0: no complete line; 1: line ready; 2: end of input.
    int poll(std::string &line) {
#ifdef _WIN32
        DWORD mode = 0;
        if (GetConsoleMode(handle, &mode)) {
            if (WaitForSingleObject(handle, 20) != WAIT_OBJECT_0)
                return 0;
            INPUT_RECORD record{};
            DWORD count = 0;
            if (!ReadConsoleInputW(handle, &record, 1, &count))
                return 2;
            if (record.EventType != KEY_EVENT || !record.Event.KeyEvent.bKeyDown)
                return 0;
            wchar_t c = record.Event.KeyEvent.uChar.UnicodeChar;
            if (!c)
                return 0;
            if (c == L'\r') {
                line = utf8(console_line);
                console_line.clear();
                DWORD written;
                WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"\r\n", 2, &written, nullptr);
                return 1;
            }
            if (c == L'\b') {
                if (!console_line.empty()) {
                    console_line.pop_back();
                    DWORD written;
                    WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), L"\b \b", 3, &written, nullptr);
                }
                return 0;
            }
            console_line += c;
            DWORD written;
            WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE), &c, 1, &written, nullptr);
            return 0;
        }
        if (GetFileType(handle) == FILE_TYPE_PIPE) {
            DWORD available = 0;
            if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr))
                return finish(line);
            if (!available) {
                Sleep(20);
                return 0;
            }
        }
        char c;
        DWORD got = 0;
        if (!ReadFile(handle, &c, 1, &got, nullptr) || !got)
            return finish(line);
#else
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        timeval timeout{0, 20000};
        int ready = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &timeout);
        if (ready == 0)
            return 0;
        if (ready < 0)
            return finish(line);
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1)
            return finish(line);
#endif
        if (c == '\n') {
            line = std::move(bytes);
            bytes.clear();
            return 1;
        }
        if (c != '\r')
            bytes += c;
        return 0;
    }

  private:
    int finish(std::string &line) {
        if (bytes.empty())
            return 2;
        line = std::move(bytes);
        bytes.clear();
        return 1;
    }
};
} // namespace cli
