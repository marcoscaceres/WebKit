/*
 * Copyright (C) 2023 Apple Inc. All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WebTransportSendStreamSink.h"

#include "WebTransportSession.h"
#include <WebCore/Exception.h>
#include <WebCore/IDLTypes.h>
#include <WebCore/JSDOMGlobalObject.h>
#include <WebCore/ScriptExecutionContextInlines.h>
#include <wtf/CompletionHandler.h>
#include <wtf/RunLoop.h>

namespace WebKit {

WebTransportSendStreamSink::WebTransportSendStreamSink(WebTransportSession& session, WebCore::WebTransportStreamIdentifier identifier)
    : m_session(session)
    , m_identifier(identifier)
{
    ASSERT(RunLoop::isMain());
}

WebTransportSendStreamSink::~WebTransportSendStreamSink()
{
}

void WebTransportSendStreamSink::write(WebCore::ScriptExecutionContext& context, JSC::JSValue value, WebCore::DOMPromiseDeferred<void>&& promise)
{
    RefPtr session = m_session.get();
    if (!session)
        return promise.reject(WebCore::Exception { WebCore::ExceptionCode::InvalidStateError });

    if (!context.globalObject())
        return promise.reject(WebCore::Exception { WebCore::ExceptionCode::InvalidStateError });

    if (m_isClosed)
        return promise.reject(WebCore::Exception { WebCore::ExceptionCode::InvalidStateError });

    auto& globalObject = *JSC::jsCast<WebCore::JSDOMGlobalObject*>(context.globalObject());
    auto scope = DECLARE_THROW_SCOPE(globalObject.vm());

    auto bufferSource = convert<WebCore::IDLUnion<WebCore::IDLArrayBuffer, WebCore::IDLArrayBufferView>>(globalObject, value);
    if (bufferSource.hasException(scope)) [[unlikely]]
        return promise.settle(WebCore::Exception { WebCore::ExceptionCode::ExistingExceptionError });

    WTF::switchOn(bufferSource.releaseReturnValue(), [&](auto&& arrayBufferOrView) {
        constexpr bool withFin { false };
        context.enqueueTaskWhenSettled(session->streamSendBytes(m_identifier, arrayBufferOrView->span(), withFin), WebCore::TaskSource::Networking, [promise = WTFMove(promise)] (auto&& exception) mutable {
            if (!exception)
                promise.settle(WebCore::Exception { WebCore::ExceptionCode::NetworkError });
            else if (*exception)
                promise.settle(WTFMove(**exception));
            else
                promise.resolve();
        });
    });
}

void WebTransportSendStreamSink::close()
{
    if (m_isClosed)
        return;
    RefPtr session = m_session.get();
    if (session)
        session->streamSendBytes(m_identifier, { }, true);
    m_isClosed = true;
}

void WebTransportSendStreamSink::error(String&&)
{
    if (m_isCancelled)
        return;
    RefPtr session = m_session.get();
    if (session) {
        // FIXME: Use error code from WebTransportError
        session->cancelSendStream(m_identifier, std::nullopt);
    }
    m_isCancelled = true;
}
}
