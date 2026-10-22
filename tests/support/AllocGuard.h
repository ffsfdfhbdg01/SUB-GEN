// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <cstddef>

namespace subgen::test
{
// Counts the calls of the global operator new on the current thread while the guard is alive (the test support
// library replaces the global operator new and delete). Read count() inside the scope, before Catch2's macros, which
// allocate themselves.
class AllocGuard
{
public:
    AllocGuard();
    AllocGuard(const AllocGuard&) = delete;
    AllocGuard& operator=(const AllocGuard&) = delete;

    std::size_t count() const;

private:
    std::size_t start;
};
} // namespace subgen::test
