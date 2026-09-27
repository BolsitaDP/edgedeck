// A dependency-free test runner. The project deliberately has no third-party
// libraries, and that applies to the tests too: a failing check prints the
// file, line and both values, and the process exit code is the failure count.
//
// The mutable state lives in TestHarness.cpp, not here, so including this header
// from several translation units does not produce duplicate symbols.
#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

namespace testing {

extern int g_failures;
extern int g_checks;

void ReportFailure(const char* file, int line, const std::string& detail);
void BeginTest(const char* name);
int Summary();

// Overloads so CHECK_EQ can print ints, bools and wide strings alike.
std::string Show(int v);
std::string Show(long long v);
std::string Show(size_t v);
std::string Show(bool v);
std::string Show(const std::wstring& v);

} // namespace testing

#define TEST(name) ::testing::BeginTest(name)

#define CHECK(cond)                                                             \
    do {                                                                        \
        ++::testing::g_checks;                                                  \
        if (!(cond)) ::testing::ReportFailure(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    } while (0)

#define CHECK_EQ(actual, expected)                                                        \
    do {                                                                                  \
        ++::testing::g_checks;                                                            \
        auto&& a_ = (actual);                                                             \
        auto&& e_ = (expected);                                                           \
        if (!(a_ == e_)) {                                                                \
            ::testing::ReportFailure(__FILE__, __LINE__,                                 \
                                     std::string(#actual " != " #expected " (") +         \
                                         ::testing::Show(a_) + " vs " +                   \
                                         ::testing::Show(e_) + ")");                      \
        }                                                                                 \
    } while (0)
