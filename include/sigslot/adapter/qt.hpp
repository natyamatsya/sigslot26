// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: palacaze/sigslot contributors

#pragma once

/**
 * @file qt.hpp
 * @brief Qt adapter for sigslot object lifetime tracking
 * 
 * Provides thread-safe adapters for QSharedPointer and QWeakPointer
 * object lifetime tracking when connected to signals.
 * 
 * @note Raw QObject pointers are not directly supported as they cannot provide
 * thread-safe weak pointer semantics. Use QSharedPointer for tracked objects.
 * 
 * ## Bridging Legacy QObject* Code
 * 
 * If you have existing code with raw QObject pointers, you can create a
 * non-owning QSharedPointer for tracking:
 * 
 * @code
 * // Legacy QObject owned elsewhere (e.g., by Qt parent or manual delete)
 * MyQObject* legacyObj = ...;
 * 
 * // Create non-owning shared pointer (null deleter = won't delete)
 * auto tracked = QSharedPointer<MyQObject>(legacyObj, [](MyQObject*){});
 * 
 * // Now you can connect with lifetime tracking
 * sig.connect(&MyQObject::slot, tracked);
 * 
 * // IMPORTANT: You must ensure 'tracked' outlives the signal connection,
 * // or manually disconnect before the QObject is destroyed.
 * @endcode
 * 
 * @warning The bridging pattern shifts responsibility to you: ensure the
 * QSharedPointer outlives the raw pointer, or disconnect manually.
 * 
 * Requires Qt 6.5+
 */

#include <type_traits>
#include <QtGlobal>
#include <QSharedPointer>

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
#error "Qt 6.5 or later is required for sigslot Qt adapter"
#endif

namespace sigslot::detail {

/**
 * @brief Thread-safe weak pointer adapter for QWeakPointer
 * 
 * Wraps QWeakPointer to provide the weak pointer interface expected by sigslot.
 * Thread-safe: lock() atomically promotes to QSharedPointer or returns null.
 * 
 * @tparam T The pointee type
 */
template<typename T>
struct qweakpointer_adapter {
    explicit qweakpointer_adapter(QWeakPointer<T> o) noexcept
        : m_ptr{std::move(o)} {}

    void reset() noexcept { m_ptr.clear(); }

    [[nodiscard]] bool expired() const noexcept { return m_ptr.isNull(); }

    [[nodiscard]] QSharedPointer<T> lock() const noexcept { return m_ptr.lock(); }

private:
    QWeakPointer<T> m_ptr;
};

} // namespace sigslot::detail


QT_BEGIN_NAMESPACE

/**
 * @brief Convert QWeakPointer to trackable adapter
 * @param p QWeakPointer to wrap
 * @return Thread-safe weak pointer adapter for lifetime tracking
 */
template<typename T>
sigslot::detail::qweakpointer_adapter<T> to_weak(QWeakPointer<T> p) {
    return sigslot::detail::qweakpointer_adapter<T>{std::move(p)};
}

/**
 * @brief Convert QSharedPointer to trackable weak pointer adapter
 * @param p QSharedPointer to create weak reference from
 * @return Thread-safe weak pointer adapter for lifetime tracking
 */
template<typename T>
sigslot::detail::qweakpointer_adapter<T> to_weak(QSharedPointer<T> p) {
    return sigslot::detail::qweakpointer_adapter<T>{p.toWeakRef()};
}

QT_END_NAMESPACE
