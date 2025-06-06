/*
 * Copyright (C) 2019-2023 Apple Inc. All rights reserved.
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

#include "config.h"
#include "WideningNumberPredictionFuzzerAgent.h"

#include "CodeBlock.h"
#include <wtf/TZoneMallocInlines.h>

namespace JSC {

WTF_MAKE_TZONE_ALLOCATED_IMPL(WideningNumberPredictionFuzzerAgent);

WideningNumberPredictionFuzzerAgent::WideningNumberPredictionFuzzerAgent(VM& vm)
    : NumberPredictionFuzzerAgent(vm)
{
}

SpeculatedType WideningNumberPredictionFuzzerAgent::getPrediction(CodeBlock* codeBlock, const CodeOrigin& codeOrigin, SpeculatedType original)
{
    Locker locker { m_lock };

    if (!(original && speculationChecked(original, SpecBytecodeNumber)))
        return original;

    if (original == SpecBytecodeNumber)
        return original;

    Vector<SpeculatedType> numberTypesNotIncludedInSpeculation;
    for (auto numberType : bytecodeNumberTypes()) {
        if (!(numberType & original))
            numberTypesNotIncludedInSpeculation.append(numberType);
    }

    unsigned numberOfTypesToAdd = m_random.getUint32(numberTypesNotIncludedInSpeculation.size() + 1);
    if (!numberOfTypesToAdd)
        return original;

    SpeculatedType generated = original;
    for (unsigned i = 0; i < numberOfTypesToAdd; i++) {
        unsigned indexOfNewType = m_random.getUint32(numberTypesNotIncludedInSpeculation.size());
        mergeSpeculation(generated, numberTypesNotIncludedInSpeculation[indexOfNewType]);
        numberTypesNotIncludedInSpeculation.removeAt(indexOfNewType);
    }

    if (Options::dumpFuzzerAgentPredictions())
        dataLogLn("WideningNumberPredictionFuzzerAgent::getPrediction name:(", codeBlock->inferredNameWithHash(), "),bytecodeIndex:(", codeOrigin.bytecodeIndex(), "),original:(", SpeculationDump(original), "),generated:(", SpeculationDump(generated), ")");

    return generated;
}

} // namespace JSC
