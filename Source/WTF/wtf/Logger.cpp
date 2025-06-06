/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
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
#include "Logger.h"

#include <mutex>
#include <wtf/HexNumber.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/text/WTFString.h>

namespace WTF {

Lock loggerObserverLock;
Lock messageHandlerLoggerObserverLock;

String Logger::LogSiteIdentifier::toString() const
{
    if (className)
        return makeString(className, "::"_s, unsafeSpan(methodName), '(', hex(objectIdentifier), ") "_s);
    return makeString(unsafeSpan(methodName), '(', hex(objectIdentifier), ") "_s);
}

String LogArgument<const void*>::toString(const void* argument)
{
    return makeString('(', hex(reinterpret_cast<uintptr_t>(argument)), ')');
}

Vector<std::reference_wrapper<Logger::Observer>>& Logger::observers()
{
    static LazyNeverDestroyed<Vector<std::reference_wrapper<Observer>>> observers;
    static std::once_flag onceKey;
    std::call_once(onceKey, [&] {
        observers.construct();
    });
    return observers;
}

Vector<std::reference_wrapper<Logger::MessageHandlerObserver>>& Logger::messageHandlerObservers()
{
    static LazyNeverDestroyed<Vector<std::reference_wrapper<MessageHandlerObserver>>> observers;
    static std::once_flag onceKey;
    std::call_once(onceKey, [&] {
        observers.construct();
    });
    return observers;
}

const Logger& emptyLogger()
{
    static NeverDestroyed<Ref<Logger>> emptyLogger = [&] {
        // Passing the wrapper as the "owner" of the logger ensures
        // no caller will every be able to enable this logger.
        auto logger = Logger::create(&emptyLogger);
        logger->setEnabled(&emptyLogger, false);
        return logger;
    }();
    return emptyLogger->get();
}

} // namespace WTF
