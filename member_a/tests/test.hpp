#pragma once
#include "fixture.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace test {
using namespace ceg5206;
inline void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
template<class Exception, class F>
void expect_throw(F fn, const char* message) {
    try { fn(); } catch (const Exception&) { return; }
    throw std::runtime_error(message);
}
template<class T>
Span<const T> read(const std::vector<T>& v) { return {v.data(), v.size()}; }
template<class T>
Span<T> write(std::vector<T>& v) { return {v.data(), v.size()}; }

void validation();
void generator();
void comparison();
void chunks();
void handoff();
void variants();
void hub();
void grid();
} // namespace test
