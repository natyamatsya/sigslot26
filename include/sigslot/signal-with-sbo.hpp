// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

#include <sigslot/signal.hpp>
#include <sigslot/signal-sbo.hpp>

namespace sigslot {

// SBO-enabled signal template for Phase 3.1 demonstration
template<typename... T>
using signal_sbo = signal<T...>;

// Future: This will replace the default slots_type when SBO integration is complete
// For now, this demonstrates the improved SBO container working independently
template<typename... T>
class signal_with_sbo : public signal_base<T...> {
    using base = signal_base<T...>;

public:
    using base::base;
    using base::operator();
    using base::connect;
    using base::disconnect;
    using base::disconnect_all;
    using base::slot_count;

    // Additional SBO-specific methods for demonstration
    bool is_using_heap_for_group(typename base::group_id gid) const {
        // This would be available when SBO is fully integrated
        return false; // Placeholder
    }

    std::size_t heap_threshold() const {
        return 3; // SBO capacity
    }
};

} // namespace sigslot
