// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

/**
 * @file test_repeat.hpp
 * @brief Test repetition helpers for catching flaky race conditions
 *
 * CI sets SIGSLOT_TEST_REPEAT=10 to run threading tests multiple times.
 * Uses Catch2's GENERATE with range() for idiomatic data-driven tests.
 *
 * Usage:
 *   TEST_CASE("my threading test", "[threading]") {
 *       auto iteration = GENERATE_REPEAT();
 *       // test body runs SIGSLOT_TEST_REPEAT times
 *       // use 'iteration' if you need the current iteration number
 *   }
 */

#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#ifndef SIGSLOT_TEST_REPEAT
#define SIGSLOT_TEST_REPEAT 1
#endif

// Idiomatic Catch2 generator for test repetition
// Returns iteration number (0 to SIGSLOT_TEST_REPEAT-1)
// Catch2 will re-run the test case for each generated value
#define GENERATE_REPEAT() Catch::Generators::range(0, SIGSLOT_TEST_REPEAT)
