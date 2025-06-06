/*
 * Copyright (C) 2010-2025 Apple Inc. All rights reserved.
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
#include "DownloadProxy.h"

#include "APIData.h"
#include "APIDownloadClient.h"
#include "APIFrameInfo.h"
#include "AuthenticationChallengeProxy.h"
#include "DownloadProxyMap.h"
#include "FrameInfoData.h"
#include "NetworkProcessMessages.h"
#include "NetworkProcessProxy.h"
#include "ProcessAssertion.h"
#include "WebPageProxy.h"
#include "WebProcessMessages.h"
#include "WebProtectionSpace.h"
#include <WebCore/MIMETypeRegistry.h>
#include <WebCore/ResourceResponseBase.h>
#include <wtf/FileSystem.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

#if PLATFORM(MAC)
#include <pal/spi/mac/QuarantineSPI.h>
#endif

#if HAVE(SEC_KEY_PROXY)
#include "SecKeyProxyStore.h"
#endif

namespace WebKit {
using namespace WebCore;

DownloadProxy::DownloadProxy(DownloadProxyMap& downloadProxyMap, WebsiteDataStore& dataStore, API::DownloadClient& client, const ResourceRequest& resourceRequest, const std::optional<FrameInfoData>& frameInfoData, WebPageProxy* originatingPage)
    : m_downloadProxyMap(downloadProxyMap)
    , m_dataStore(&dataStore)
    , m_client(client)
    , m_downloadID(DownloadID::generate())
    , m_request(resourceRequest)
    , m_originatingPage(originatingPage)
    , m_frameInfo(frameInfoData ? API::FrameInfo::create(FrameInfoData { *frameInfoData }, originatingPage) : API::FrameInfo::create(legacyEmptyFrameInfo(ResourceRequest { URL { aboutBlankURL() } }), originatingPage))
#if HAVE(MODERN_DOWNLOADPROGRESS)
    , m_assertion(ProcessAssertion::create(getCurrentProcessID(), "WebKit DownloadProxy DecideDestination"_s, ProcessAssertionType::FinishTaskInterruptable))
#endif
{
}

DownloadProxy::~DownloadProxy()
{
    if (m_didStartCallback)
        m_didStartCallback(nullptr);
}

static RefPtr<API::Data> createData(std::span<const uint8_t> data)
{
    if (data.empty())
        return nullptr;
    return API::Data::create(data);
}

void DownloadProxy::cancel(CompletionHandler<void(API::Data*)>&& completionHandler)
{
    m_downloadIsCancelled = true;
    if (m_dataStore) {
        protectedDataStore()->protectedNetworkProcess()->sendWithAsyncReply(Messages::NetworkProcess::CancelDownload(m_downloadID), [weakThis = WeakPtr { *this }, completionHandler = WTFMove(completionHandler)] (std::span<const uint8_t> resumeData) mutable {
            RefPtr protectedThis = weakThis.get();
            if (!protectedThis)
                return completionHandler(nullptr);
            protectedThis->m_legacyResumeData = createData(resumeData);
            completionHandler(protectedThis->m_legacyResumeData.get());
            if (RefPtr downloadProxyMap = protectedThis->m_downloadProxyMap.get())
                downloadProxyMap->downloadFinished(*protectedThis);
        });
    } else
        completionHandler(nullptr);
}

void DownloadProxy::invalidate()
{
    ASSERT(m_dataStore);
    m_dataStore = nullptr;
}

void DownloadProxy::processDidClose()
{
    protectedClient()->processDidCrash(*this);
}

WebPageProxy* DownloadProxy::originatingPage() const
{
    return m_originatingPage.get();
}

void DownloadProxy::didStart(const ResourceRequest& request, const String& suggestedFilename)
{
    m_request = request;
    m_suggestedFilename = suggestedFilename;

    if (m_redirectChain.isEmpty() || m_redirectChain.last() != request.url())
        m_redirectChain.append(request.url());

    if (m_didStartCallback)
        m_didStartCallback(this);
    protectedClient()->legacyDidStart(*this);
}

void DownloadProxy::didReceiveAuthenticationChallenge(AuthenticationChallenge&& authenticationChallenge, AuthenticationChallengeIdentifier challengeID)
{
    RefPtr dataStore = m_dataStore;
    if (!dataStore)
        return;

    auto authenticationChallengeProxy = AuthenticationChallengeProxy::create(WTFMove(authenticationChallenge), challengeID, dataStore->networkProcess().connection(), nullptr);
    protectedClient()->didReceiveAuthenticationChallenge(*this, authenticationChallengeProxy.get());
}

void DownloadProxy::willSendRequest(ResourceRequest&& proposedRequest, const ResourceResponse& redirectResponse, CompletionHandler<void(ResourceRequest&&)>&& completionHandler)
{
    protectedClient()->willSendRequest(*this, WTFMove(proposedRequest), redirectResponse, [this, protectedThis = Ref { *this }, completionHandler = WTFMove(completionHandler)] (ResourceRequest&& newRequest) mutable {
        m_redirectChain.append(newRequest.url());
        completionHandler(WTFMove(newRequest));
    });
}

void DownloadProxy::didReceiveData(uint64_t bytesWritten, uint64_t totalBytesWritten, uint64_t totalBytesExpectedToWrite)
{
    protectedClient()->didReceiveData(*this, bytesWritten, totalBytesWritten, totalBytesExpectedToWrite);
}

void DownloadProxy::decideDestinationWithSuggestedFilename(const WebCore::ResourceResponse& response, String&& suggestedFilename, DecideDestinationCallback&& completionHandler)
{
    RELEASE_LOG_INFO_IF(!response.expectedContentLength(), Network, "DownloadProxy::decideDestinationWithSuggestedFilename expectedContentLength is null");

    // As per https://html.spec.whatwg.org/#as-a-download (step 2), the filename from the Content-Disposition header
    // should override the suggested filename from the download attribute.
    if (response.isAttachmentWithFilename() || (suggestedFilename.isEmpty() && m_suggestedFilename.isEmpty()))
        suggestedFilename = response.suggestedFilename();
    else if (!m_suggestedFilename.isEmpty())
        suggestedFilename = m_suggestedFilename;
    suggestedFilename = MIMETypeRegistry::appendFileExtensionIfNecessary(suggestedFilename, response.mimeType());

    protectedClient()->decideDestinationWithSuggestedFilename(*this, response, ResourceResponseBase::sanitizeSuggestedFilename(suggestedFilename), [this, protectedThis = Ref { *this }, completionHandler = WTFMove(completionHandler)] (AllowOverwrite allowOverwrite, String destination) mutable {
        SandboxExtension::Handle sandboxExtensionHandle;
        if (!destination.isNull()) {
            if (auto handle = SandboxExtension::createHandle(destination, SandboxExtension::Type::ReadWrite))
                sandboxExtensionHandle = WTFMove(*handle);
        }

        setDestinationFilename(destination);

        protectedClient()->decidePlaceholderPolicy(*this, [completionHandler = WTFMove(completionHandler), destination = WTFMove(destination), sandboxExtensionHandle = WTFMove(sandboxExtensionHandle), allowOverwrite] (WebKit::UseDownloadPlaceholder usePlaceholder, const URL& url) mutable {

            SandboxExtension::Handle placeHolderSandboxExtensionHandle;
            Vector<uint8_t> bookmarkData;
            Vector<uint8_t> activityTokenData;
#if HAVE(MODERN_DOWNLOADPROGRESS)
            bookmarkData = bookmarkDataForURL(url);
            activityTokenData = activityAccessToken();
#else
            if (auto handle = SandboxExtension::createHandle(url.fileSystemPath(), SandboxExtension::Type::ReadWrite))
                placeHolderSandboxExtensionHandle = WTFMove(*handle);
#endif
            completionHandler(destination, WTFMove(sandboxExtensionHandle), allowOverwrite, usePlaceholder, url, WTFMove(placeHolderSandboxExtensionHandle), bookmarkData.span(), activityTokenData.span());
        });
    });
}

void DownloadProxy::didCreateDestination(const String& path)
{
    protectedClient()->didCreateDestination(*this, path);
}

#if PLATFORM(MAC)
void DownloadProxy::updateQuarantinePropertiesIfPossible()
{
    auto fileURL = URL::fileURLWithFileSystemPath(m_destinationFilename);
    auto path = fileURL.fileSystemPath().utf8();

    auto file = std::unique_ptr<_qtn_file, QuarantineFileDeleter>(qtn_file_alloc());
    if (!file)
        return;

    auto error = qtn_file_init_with_path(file.get(), path.data());
    if (error)
        return;

    uint32_t flags = qtn_file_get_flags(file.get());
    ASSERT_WITH_MESSAGE(flags & QTN_FLAG_HARD, "Downloaded files written by the sandboxed network process should have QTN_FLAG_HARD");
    flags &= ~QTN_FLAG_HARD;
    error = qtn_file_set_flags(file.get(), flags);
    if (error)
        return;

    qtn_file_apply_to_path(file.get(), path.data());
}
#endif

void DownloadProxy::didFinish()
{
#if PLATFORM(MAC)
    updateQuarantinePropertiesIfPossible();
#endif
    protectedClient()->didFinish(*this);
    if (m_downloadIsCancelled)
        return;

    // This can cause the DownloadProxy object to be deleted.
    if (RefPtr downloadProxyMap = m_downloadProxyMap.get())
        downloadProxyMap->downloadFinished(*this);
}

void DownloadProxy::didFail(const ResourceError& error, std::span<const uint8_t> resumeData)
{
    if (m_downloadIsCancelled)
        return;

    m_legacyResumeData = createData(resumeData);

    protectedClient()->didFail(*this, error, m_legacyResumeData.get());

    // This can cause the DownloadProxy object to be deleted.
    if (RefPtr downloadProxyMap = m_downloadProxyMap.get())
        downloadProxyMap->downloadFinished(*this);
}

void DownloadProxy::setClient(Ref<API::DownloadClient>&& client)
{
    m_client = WTFMove(client);
}

Ref<API::DownloadClient> DownloadProxy::protectedClient() const
{
    return m_client;
}

} // namespace WebKit

