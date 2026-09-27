#include "Diagnostics.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace Diagnostics {
namespace {

std::wstring Directory() {
    wchar_t buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    const std::wstring dir = std::wstring(buf) + L"\\EdgeDeck";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::mutex g_mutex;
constexpr long long kMaxBytes = 512 * 1024;

} // namespace

std::wstring LogPath() {
    const std::wstring dir = Directory();
    return dir.empty() ? L"" : dir + L"\\edgedeck.log";
}

std::string LastErrorText() {
    const DWORD code = GetLastError();
    if (code == 0) return "no error";

    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string message;
    if (length && buffer) {
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), nullptr, 0,
                                              nullptr, nullptr);
        if (bytes > 0) {
            message.resize(static_cast<size_t>(bytes));
            WideCharToMultiByte(CP_UTF8, 0, buffer, static_cast<int>(length), message.data(), bytes,
                                nullptr, nullptr);
            while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
                message.pop_back();
            }
        }
    }
    if (buffer) LocalFree(buffer);

    char head[64];
    sprintf_s(head, sizeof(head), "error %lu", code);
    if (message.empty()) return head;
    return std::string(head) + " (" + message + ")";
}

void Write(const char* level, const char* format, ...) {
    const std::wstring path = LogPath();
    if (path.empty()) return;

    char body[1024];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
    va_end(args);
    if (written < 0) return;

    SYSTEMTIME st{};
    GetLocalTime(&st);

    std::lock_guard<std::mutex> lock(g_mutex);

    // Roll the file over rather than letting it grow without bound. One and a
    // half megabytes of log is far more than a normal session ever produces, so
    // reaching the cap always means something is looping.
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) &&
        data.nFileSizeHigh == 0 && data.nFileSizeLow > kMaxBytes) {
        const std::wstring old = path + L".old";
        DeleteFileW(old.c_str());
        MoveFileExW(path.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING);
    }

    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"a") != 0 || !file) return;
    fprintf(file, "%02d:%02d:%02d %-5s %s\n", st.wHour, st.wMinute, st.wSecond, level, body);
    fclose(file);
}

void Error(const char* format, ...) {
    char body[1024];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
    va_end(args);
    if (written < 0) return;
    Write("error", "%s", body);
}

void Info(const char* format, ...) {
    char body[1024];
    va_list args;
    va_start(args, format);
    const int written = vsnprintf_s(body, sizeof(body), _TRUNCATE, format, args);
    va_end(args);
    if (written < 0) return;
    Write("info", "%s", body);
}

} // namespace Diagnostics
