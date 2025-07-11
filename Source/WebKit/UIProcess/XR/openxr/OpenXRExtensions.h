/*
 * Copyright (C) 2025 Igalia, S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * aint with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#if ENABLE(WEBXR) && USE(OPENXR)

#include "OpenXRUtils.h"

#include <wtf/Noncopyable.h>
#include <wtf/TZoneMalloc.h>
#include <wtf/Vector.h>

namespace WebKit {

class OpenXRExtensions final {
    WTF_MAKE_TZONE_ALLOCATED(OpenXRExtensions);
    WTF_MAKE_NONCOPYABLE(OpenXRExtensions);
public:
    static std::unique_ptr<OpenXRExtensions> create();
    OpenXRExtensions(Vector<XrExtensionProperties>&&);
    ~OpenXRExtensions();

    void loadMethods(XrInstance);
    bool isExtensionSupported(std::span<const char>) const;
    struct OpenXRExtensionMethods;
    const OpenXRExtensionMethods& methods() const { return *m_methods; }

private:
    Vector<XrExtensionProperties> m_extensions;
    std::unique_ptr<OpenXRExtensionMethods> m_methods;
};

} // namespace WebKit

#endif // ENABLE(WEBXR) && USE(OPENXR)
