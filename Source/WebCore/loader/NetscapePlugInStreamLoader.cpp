/*
 * Copyright (C) 2006-2025 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer. 
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution. 
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "NetscapePlugInStreamLoader.h"

#include "DocumentLoader.h"
#include "LoaderStrategy.h"
#include "PlatformStrategies.h"
#include <wtf/CompletionHandler.h>
#include <wtf/Ref.h>

#if ENABLE(CONTENT_EXTENSIONS)
#include "ResourceLoadInfo.h"
#endif

namespace WebCore {

// FIXME: Skip Content Security Policy check when associated plugin element is in a user agent shadow tree.
// See <https://bugs.webkit.org/show_bug.cgi?id=146663>.
NetscapePlugInStreamLoader::NetscapePlugInStreamLoader(LocalFrame& frame, NetscapePlugInStreamLoaderClient& client)
    : ResourceLoader(frame, ResourceLoaderOptions(
        SendCallbackPolicy::SendCallbacks,
        ContentSniffingPolicy::SniffContent,
        DataBufferingPolicy::DoNotBufferData,
        StoredCredentialsPolicy::Use,
        ClientCredentialPolicy::MayAskClientForCredentials,
        FetchOptions::Credentials::Include,
        SecurityCheckPolicy::SkipSecurityCheck,
        FetchOptions::Mode::NoCors,
        CertificateInfoPolicy::DoNotIncludeCertificateInfo,
        ContentSecurityPolicyImposition::DoPolicyCheck,
        DefersLoadingPolicy::AllowDefersLoading,
        CachingPolicy::AllowCaching))
    , m_client(client)
{
#if ENABLE(CONTENT_EXTENSIONS)
    m_resourceType = { ContentExtensions::ResourceType::Other };
#endif
}

NetscapePlugInStreamLoader::~NetscapePlugInStreamLoader() = default;

void NetscapePlugInStreamLoader::create(LocalFrame& frame, NetscapePlugInStreamLoaderClient& client, ResourceRequest&& request, CompletionHandler<void(RefPtr<NetscapePlugInStreamLoader>&&)>&& completionHandler)
{
    if (request.isNull())
        return completionHandler(nullptr);

    Ref loader = adoptRef(*new NetscapePlugInStreamLoader(frame, client));
    loader->init(WTFMove(request), [loader, completionHandler = WTFMove(completionHandler)] (bool initialized) mutable {
        if (!initialized)
            return completionHandler(nullptr);
        completionHandler(WTFMove(loader));
    });
}

bool NetscapePlugInStreamLoader::isDone() const
{
    return !m_client;
}

void NetscapePlugInStreamLoader::releaseResources()
{
    m_client = nullptr;
    ResourceLoader::releaseResources();
}

void NetscapePlugInStreamLoader::init(ResourceRequest&& request, CompletionHandler<void(bool)>&& completionHandler)
{
    ResourceLoader::init(WTFMove(request), [this, protectedThis = Ref { *this }, completionHandler = WTFMove(completionHandler)] (bool success) mutable {
        if (!success)
            return completionHandler(false);
        ASSERT(!reachedTerminalState());
        protectedDocumentLoader()->addPlugInStreamLoader(*this);
        m_isInitialized = true;
        completionHandler(true);
    });
}

void NetscapePlugInStreamLoader::willSendRequest(ResourceRequest&& request, const ResourceResponse& redirectResponse, CompletionHandler<void(ResourceRequest&&)>&& callback)
{
    if (!m_client)
        return;

    m_client->willSendRequest(this, WTFMove(request), redirectResponse, [protectedThis = Ref { *this }, redirectResponse, callback = WTFMove(callback)] (ResourceRequest&& request) mutable {
        if (!request.isNull())
            protectedThis->willSendRequestInternal(WTFMove(request), redirectResponse, WTFMove(callback));
        else
            callback({ });
    });
}

void NetscapePlugInStreamLoader::didReceiveResponse(ResourceResponse&& response, CompletionHandler<void()>&& policyCompletionHandler)
{
    Ref<NetscapePlugInStreamLoader> protectedThis(*this);
    CompletionHandlerCallingScope completionHandlerCaller(WTFMove(policyCompletionHandler));

    if (m_client)
        m_client->didReceiveResponse(this, response);

    // Don't continue if the stream is cancelled
    if (!m_client)
        return;

    ResourceResponse responseForClosure = response;
    ResourceLoader::didReceiveResponse(WTFMove(response), [this, protectedThis = WTFMove(protectedThis), response = WTFMove(responseForClosure), completionHandlerCaller = WTFMove(completionHandlerCaller)]() mutable {
        // Don't continue if the stream is cancelled
        if (!m_client)
            return;

        if (!response.isInHTTPFamily())
            return;

        if (m_client->wantsAllStreams())
            return;

        // Status code can be null when serving from a Web archive.
        if (response.httpStatusCode() && (response.httpStatusCode() < 100 || response.httpStatusCode() >= 400))
            cancel(platformStrategies()->loaderStrategy()->fileDoesNotExistError(response));
    });
}

void NetscapePlugInStreamLoader::didReceiveData(const SharedBuffer& buffer, long long encodedDataLength, DataPayloadType dataPayloadType)
{
    Ref protectedThis { *this };

    if (m_client)
        m_client->didReceiveData(this, buffer);

    ResourceLoader::didReceiveData(buffer, encodedDataLength, dataPayloadType);
}

void NetscapePlugInStreamLoader::didFinishLoading(const NetworkLoadMetrics& networkLoadMetrics)
{
    Ref protectedThis { *this };

    notifyDone();

    if (m_client)
        m_client->didFinishLoading(this);
    ResourceLoader::didFinishLoading(networkLoadMetrics);
}

void NetscapePlugInStreamLoader::didFail(const ResourceError& error)
{
    Ref protectedThis { *this };

    notifyDone();

    if (m_client)
        m_client->didFail(this, error);
    ResourceLoader::didFail(error);
}

void NetscapePlugInStreamLoader::willCancel(const ResourceError& error)
{
    if (m_client)
        m_client->didFail(this, error);
}

void NetscapePlugInStreamLoader::didCancel(LoadWillContinueInAnotherProcess)
{
    notifyDone();
}

void NetscapePlugInStreamLoader::notifyDone()
{
    if (!m_isInitialized)
        return;

    if (RefPtr documentLoader = this->documentLoader())
        documentLoader->removePlugInStreamLoader(*this);
}


}
