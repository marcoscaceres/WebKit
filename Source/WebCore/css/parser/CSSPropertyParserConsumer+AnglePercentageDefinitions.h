/*
 * Copyright (C) 2024 Samuel Weinig <sam@webkit.org>
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
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "CSSPropertyParserConsumer+AngleDefinitions.h"
#include "CSSPropertyParserConsumer+MetaConsumerDefinitions.h"

namespace WebCore {
namespace CSSPropertyParserHelpers {

struct AnglePercentageValidator {
    static constexpr std::optional<CSS::AnglePercentageUnit> validate(CSSUnitType unitType, CSS::PropertyParserState& state, CSSPropertyParserOptions options)
    {
        // NOTE: Percentages are handled explicitly by the PercentageValidator, so this only
        // needs to be concerned with the Angle units.
        if (auto result = AngleValidator::validate(unitType, state, options))
            return static_cast<CSS::AnglePercentageUnit>(*result);
        return std::nullopt;
    }

    template<auto R, typename V> static bool isValid(CSS::AnglePercentageRaw<R, V> raw, CSSPropertyParserOptions)
    {
        // Values other than 0 and +/-∞ are not supported for <angle-percentage> numeric ranges currently.
        return isValidNonCanonicalizableDimensionValue(raw);
    }

    static bool shouldAcceptUnitlessValue(double value, CSS::PropertyParserState& state, CSSPropertyParserOptions options)
    {
        return AngleValidator::shouldAcceptUnitlessValue(value, state, options);
    }
};

template<auto R, typename V> struct ConsumerDefinition<CSS::AnglePercentage<R, V>> {
    using FunctionToken = FunctionConsumerForCalcValues<CSS::AnglePercentage<R, V>>;
    using DimensionToken = DimensionConsumer<CSS::AnglePercentage<R, V>, AnglePercentageValidator>;
    using PercentageToken = PercentageConsumer<CSS::AnglePercentage<R, V>, AnglePercentageValidator>;
    using NumberToken = NumberConsumerForUnitlessValues<CSS::AnglePercentage<R, V>, AnglePercentageValidator, CSS::AngleUnit::Deg>;
};

} // namespace CSSPropertyParserHelpers
} // namespace WebCore
