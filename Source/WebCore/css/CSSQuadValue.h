/**
 * Copyright (C) 2023 Apple Inc. All right reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "Quad.h"

namespace WebCore {

class CSSQuadValue final : public CSSValue {
public:
    static Ref<CSSQuadValue> create(Quad);
    static Ref<CSSQuadValue> create(Ref<CSSValue>);
    static Ref<CSSQuadValue> create(Ref<CSSValue>, Ref<CSSValue>);
    static Ref<CSSQuadValue> create(Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>);
    static Ref<CSSQuadValue> create(Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>, Ref<CSSValue>);

    const Quad& quad() const { return m_quad; }

    String customCSSText(const CSS::SerializationContext&) const;
    bool equals(const CSSQuadValue&) const;
    bool canBeCoalesced() const;

private:
    explicit CSSQuadValue(Quad);
    bool m_coalesceIdenticalValues { true };
    Quad m_quad;
};

inline const Quad& CSSValue::quad() const
{
    return downcast<CSSQuadValue>(*this).quad();
}

inline Ref<CSSQuadValue> CSSQuadValue::create(Ref<CSSValue> a)
{
    return CSSQuadValue::create(Quad { a, a, a, a });
}

inline Ref<CSSQuadValue> CSSQuadValue::create(Ref<CSSValue> a, Ref<CSSValue> b)
{
    return CSSQuadValue::create(Quad { a, b, a, b });
}

inline Ref<CSSQuadValue> CSSQuadValue::create(Ref<CSSValue> a, Ref<CSSValue> b, Ref<CSSValue> c)
{
    return CSSQuadValue::create(Quad { a, b, c, b });
}

inline Ref<CSSQuadValue> CSSQuadValue::create(Ref<CSSValue> a, Ref<CSSValue> b, Ref<CSSValue> c, Ref<CSSValue> d)
{
    return CSSQuadValue::create(Quad { WTFMove(a), WTFMove(b), WTFMove(c), WTFMove(d) });
}

} // namespace WebCore

SPECIALIZE_TYPE_TRAITS_CSS_VALUE(CSSQuadValue, isQuad())
