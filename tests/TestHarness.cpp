#include "TestHarness.h"

namespace testing {

int g_failures = 0;
int g_checks = 0;

void ReportFailure(const char* file, int line, const std::string& detail) {
    ++g_failures;
    std::printf("  FAIL %s:%d\n    %s\n", file, line, detail.c_str());
}

void BeginTest(const char* name) { std::printf("- %s\n", name); }

int Summary() {
    std::printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

std::string Show(int v) { return std::to_string(v); }
std::string Show(long long v) { return std::to_string(v); }
std::string Show(size_t v) { return std::to_string(v); }
std::string Show(bool v) { return v ? "true" : "false"; }

std::string Show(const std::wstring& v) {
    if (v.empty()) return "\"\"";
    const int needed = WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1) return "\"\"";
    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, v.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return "\"" + out + "\"";
}

} // namespace testing
