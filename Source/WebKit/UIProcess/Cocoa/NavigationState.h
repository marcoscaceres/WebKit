/*
 * Copyright (C) 2014-2025 Apple Inc. All rights reserved.
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

#pragma once

#import "WKFoundation.h"

#import "APIHistoryClient.h"
#import "APINavigationClient.h"
#import "PageLoadState.h"
#import "ProcessTerminationReason.h"
#import "ProcessThrottler.h"
#import <wtf/RetainPtr.h>
#import <wtf/RunLoop.h>
#import <wtf/TZoneMalloc.h>
#import <wtf/UniqueRef.h>
#import <wtf/WeakObjCPtr.h>
#import <wtf/WeakPtr.h>

@class WKWebView;
@protocol WKHistoryDelegatePrivate;
@protocol WKNavigationDelegate;

namespace API {
class Navigation;
}

namespace WebCore {
class SecurityOriginData;
}

namespace WebKit {

struct WebNavigationDataStore;
class WebPageLoadTiming;

class NavigationState final : public PageLoadState::Observer {
    WTF_MAKE_TZONE_ALLOCATED(NavigationState);
public:
    explicit NavigationState(WKWebView *);
    ~NavigationState();

    void ref() const final;
    void deref() const final;

    static NavigationState* fromWebPage(WebPageProxy&);

    UniqueRef<API::NavigationClient> createNavigationClient();
    UniqueRef<API::HistoryClient> createHistoryClient();

    RetainPtr<id<WKNavigationDelegate>> navigationDelegate() const;
    void setNavigationDelegate(id<WKNavigationDelegate>);

    RetainPtr<id<WKHistoryDelegatePrivate>> historyDelegate() const;
    void setHistoryDelegate(id<WKHistoryDelegatePrivate>);

    // Called by the page client.
    void navigationGestureDidBegin();
    void navigationGestureWillEnd(bool willNavigate, WebBackForwardListItem&);
    void navigationGestureDidEnd(bool willNavigate, WebBackForwardListItem&);
    void willRecordNavigationSnapshot(WebBackForwardListItem&);
    void navigationGestureSnapshotWasRemoved();

#if PLATFORM(IOS_FAMILY)
    void didRequestPasswordForQuickLookDocument();
    void didStopRequestingPasswordForQuickLookDocument();
#endif

    void didFirstPaint();

#if USE(RUNNINGBOARD)
    enum class NetworkActivityReleaseReason { LoadCompleted, ScreenLocked };
    void releaseNetworkActivity(NetworkActivityReleaseReason);
#endif

    void didGeneratePageLoadTiming(const WebPageLoadTiming&);

private:
    class NavigationClient final : public API::NavigationClient {
        WTF_MAKE_TZONE_ALLOCATED(NavigationClient);
    public:
        explicit NavigationClient(NavigationState&);
        ~NavigationClient();

    private:
        RefPtr<NavigationState> protectedNavigationState() const { return m_navigationState.get(); }

        void didStartProvisionalNavigation(WebPageProxy&, const WebCore::ResourceRequest&, API::Navigation*, API::Object*) override;
        void didStartProvisionalLoadForFrame(WebPageProxy&, WebCore::ResourceRequest&&, FrameInfoData&&) override;
        void didReceiveServerRedirectForProvisionalNavigation(WebPageProxy&, API::Navigation*, API::Object*) override;
        void willPerformClientRedirect(WebPageProxy&, WTF::String&&, double) override;
        void didPerformClientRedirect(WebPageProxy&, const WTF::String&, const WTF::String&) override;
        void didCancelClientRedirect(WebPageProxy&) override;
        void didFailProvisionalNavigationWithError(WebPageProxy&, FrameInfoData&&, API::Navigation*, const URL&, const WebCore::ResourceError&, API::Object*) override;
        void didFailProvisionalLoadWithErrorForFrame(WebPageProxy&, WebCore::ResourceRequest&&, const WebCore::ResourceError&, FrameInfoData&&) override;
        void didCommitNavigation(WebPageProxy&, API::Navigation*, API::Object*) override;
        void didCommitLoadForFrame(WebKit::WebPageProxy&, WebCore::ResourceRequest&&, FrameInfoData&&) override;
        void didFinishDocumentLoad(WebPageProxy&, API::Navigation*, API::Object*) override;
        void didFinishNavigation(WebPageProxy&, API::Navigation*, API::Object*) override;
        void didFinishLoadForFrame(WebPageProxy&, WebCore::ResourceRequest&&, FrameInfoData&&) override;
        void didBlockLoadToKnownTracker(WebPageProxy&, const URL&) override;
        void didFailNavigationWithError(WebPageProxy&, const FrameInfoData&, API::Navigation*, const URL&, const WebCore::ResourceError&, API::Object*) override;
        void didFailLoadWithErrorForFrame(WebPageProxy&, WebCore::ResourceRequest&&, const WebCore::ResourceError&, FrameInfoData&&) override;
        void didSameDocumentNavigation(WebPageProxy&, API::Navigation*, SameDocumentNavigationType, API::Object*) override;
        void didApplyLinkDecorationFiltering(WebPageProxy&, const URL&, const URL&) override;
        void didPromptForStorageAccess(WebPageProxy&, const String& topFrameDomain, const String& subFrameDomain, bool hasQuirk) override;

        void renderingProgressDidChange(WebPageProxy&, OptionSet<WebCore::LayoutMilestone>) override;

        bool shouldBypassContentModeSafeguards() const final;

        void didReceiveAuthenticationChallenge(WebPageProxy&, AuthenticationChallengeProxy&) override;
        void shouldAllowLegacyTLS(WebPageProxy&, AuthenticationChallengeProxy&, CompletionHandler<void(bool)>&&) final;
        void didNegotiateModernTLS(const URL&) final;
        bool processDidTerminate(WebPageProxy&, ProcessTerminationReason) override;
        void processDidBecomeResponsive(WebPageProxy&) override;
        void processDidBecomeUnresponsive(WebPageProxy&) override;

        void legacyWebCryptoMasterKey(WebPageProxy&, CompletionHandler<void(std::optional<Vector<uint8_t>>&&)>&&) override;

        void navigationActionDidBecomeDownload(WebPageProxy&, API::NavigationAction&, DownloadProxy&) final;
        void navigationResponseDidBecomeDownload(WebPageProxy&, API::NavigationResponse&, DownloadProxy&) final;
        void contextMenuDidCreateDownload(WebPageProxy&, DownloadProxy&) final;

#if USE(QUICK_LOOK)
        void didStartLoadForQuickLookDocumentInMainFrame(const WTF::String& fileName, const WTF::String& uti) override;
        void didFinishLoadForQuickLookDocumentInMainFrame(const WebCore::FragmentedSharedBuffer&) override;
#endif

        bool didChangeBackForwardList(WebPageProxy&, WebBackForwardListItem*, const Vector<Ref<WebBackForwardListItem>>&) final;
        void shouldGoToBackForwardListItem(WebPageProxy&, WebBackForwardListItem&, bool inBackForwardCache, CompletionHandler<void(bool)>&&) final;

#if ENABLE(CONTENT_EXTENSIONS)
        void contentRuleListNotification(WebPageProxy&, URL&&, WebCore::ContentRuleListResults&&) final;
#endif
        void decidePolicyForNavigationAction(WebPageProxy&, Ref<API::NavigationAction>&&, Ref<WebFramePolicyListenerProxy>&&) override;
        void decidePolicyForNavigationResponse(WebPageProxy&, Ref<API::NavigationResponse>&&, Ref<WebFramePolicyListenerProxy>&&) override;

#if HAVE(APP_SSO)
        void decidePolicyForSOAuthorizationLoad(WebPageProxy&, SOAuthorizationLoadPolicy, const String&, CompletionHandler<void(SOAuthorizationLoadPolicy)>&&) override;
#endif

        WeakPtr<NavigationState> m_navigationState;
    };
    
    class HistoryClient final : public API::HistoryClient {
        WTF_MAKE_TZONE_ALLOCATED(HistoryClient);
    public:
        explicit HistoryClient(NavigationState&);
        ~HistoryClient();
        
    private:
        void didNavigateWithNavigationData(WebPageProxy&, const WebNavigationDataStore&) override;
        void didPerformClientRedirect(WebPageProxy&, const WTF::String&, const WTF::String&) override;
        void didPerformServerRedirect(WebPageProxy&, const WTF::String&, const WTF::String&) override;
        void didUpdateHistoryTitle(WebPageProxy&, const WTF::String&, const WTF::String&) override;
        
        WeakPtr<NavigationState> m_navigationState;
    };

    // PageLoadState::Observer
    void willChangeIsLoading() override;
    void didChangeIsLoading() override;
    void willChangeTitle() override;
    void didChangeTitle() override;
    void willChangeActiveURL() override;
    void didChangeActiveURL() override;
    void willChangeHasOnlySecureContent() override;
    void didChangeHasOnlySecureContent() override;
    void willChangeNegotiatedLegacyTLS() override;
    void didChangeNegotiatedLegacyTLS() override;
    void willChangeWasPrivateRelayed() override;
    void didChangeWasPrivateRelayed() override;
    void willChangeEstimatedProgress() override;
    void didChangeEstimatedProgress() override;
    void willChangeCanGoBack() override;
    void didChangeCanGoBack() override;
    void willChangeCanGoForward() override;
    void didChangeCanGoForward() override;
    void willChangeNetworkRequestsInProgress() override;
    void didChangeNetworkRequestsInProgress() override;
    void willChangeCertificateInfo() override;
    void didChangeCertificateInfo() override;
    void willChangeWebProcessIsResponsive() override;
    void didChangeWebProcessIsResponsive() override;
    void didSwapWebProcesses() override;

#if USE(RUNNINGBOARD)
    void releaseNetworkActivityAfterLoadCompletion() { releaseNetworkActivity(NetworkActivityReleaseReason::LoadCompleted); }
#endif

    RetainPtr<WKWebView> webView() const { return m_webView.get(); }

    WeakObjCPtr<WKWebView> m_webView;
    WeakObjCPtr<id<WKNavigationDelegate>> m_navigationDelegate;

    struct {
        bool webViewDecidePolicyForNavigationActionDecisionHandler : 1;
        bool webViewDecidePolicyForNavigationActionWithPreferencesDecisionHandler : 1;
        bool webViewDecidePolicyForNavigationActionWithPreferencesUserInfoDecisionHandler : 1;
        bool webViewDecidePolicyForNavigationResponseDecisionHandler : 1;

        bool webViewDidStartProvisionalNavigation : 1;
        bool webViewDidStartProvisionalLoadWithRequestInFrame : 1;
        bool webViewDidReceiveServerRedirectForProvisionalNavigation : 1;
        bool webViewDidFailProvisionalNavigationWithError : 1;
        bool webViewDidFailProvisionalLoadWithRequestInFrameWithError : 1;
        bool webViewNavigationDidFailProvisionalLoadInSubframeWithError : 1;
        bool webViewDidFailProvisionalLoadWithRequestInFrame : 1;
        bool webViewWillPerformClientRedirect : 1;
        bool webViewDidPerformClientRedirect : 1;
        bool webViewDidCancelClientRedirect : 1;
        bool webViewDidCommitNavigation : 1;
        bool webViewDidCommitLoadWithRequestInFrame : 1;
        bool webViewNavigationDidFinishDocumentLoad : 1;
        bool webViewDidFinishNavigation : 1;
        bool webViewDidFinishLoadWithRequestInFrame : 1;
        bool webViewDidFailNavigationWithError : 1;
        bool webViewDidFailNavigationWithErrorUserInfo : 1;
        bool webViewDidFailLoadWithRequestInFrameWithError : 1;
        bool webViewNavigationDidSameDocumentNavigation : 1;
        bool webViewDidFailLoadDueToNetworkConnectionIntegrityWithURL : 1;
        bool webViewDidChangeLookalikeCharactersFromURLToURL : 1;
        bool webViewDidPromptForStorageAccessForSubFrameDomainForQuirk : 1;

        bool webViewRenderingProgressDidChange : 1;
        bool webViewDidReceiveAuthenticationChallengeCompletionHandler : 1;
        bool webViewAuthenticationChallengeShouldAllowLegacyTLS : 1;
        bool webViewAuthenticationChallengeShouldAllowDeprecatedTLS : 1;
        bool webViewDidNegotiateModernTLS : 1;
        bool webViewWebContentProcessDidTerminate : 1;
        bool webViewWebContentProcessDidTerminateWithReason : 1;
        bool webViewWebProcessDidCrash : 1;
        bool webViewWebProcessDidBecomeResponsive : 1;
        bool webViewWebProcessDidBecomeUnresponsive : 1;
        bool webCryptoMasterKeyForWebView : 1;
        bool webCryptoMasterKeyForWebViewCompletionHandler : 1;
        bool navigationActionDidBecomeDownload : 1;
        bool navigationResponseDidBecomeDownload : 1;
        bool contextMenuDidCreateDownload;
        bool webViewDidBeginNavigationGesture : 1;
        bool webViewWillEndNavigationGestureWithNavigationToBackForwardListItem : 1;
        bool webViewDidEndNavigationGestureWithNavigationToBackForwardListItem : 1;
        bool webViewWillSnapshotBackForwardListItem : 1;
        bool webViewNavigationGestureSnapshotWasRemoved : 1;
        bool webViewURLContentRuleListIdentifiersNotifications : 1;
        bool webViewContentRuleListWithIdentifierPerformedActionForURL : 1;
#if USE(QUICK_LOOK)
        bool webViewDidStartLoadForQuickLookDocumentInMainFrame : 1;
        bool webViewDidFinishLoadForQuickLookDocumentInMainFrame : 1;
#endif
        bool webViewDidRequestPasswordForQuickLookDocument : 1;
        bool webViewDidStopRequestingPasswordForQuickLookDocument : 1;

        bool webViewBackForwardListItemAddedRemoved : 1;
        bool webViewWillGoToBackForwardListItemInBackForwardCache : 1;
        bool webViewShouldGoToBackForwardListItemInBackForwardCacheCompletionHandler : 1;
        bool webViewShouldGoToBackForwardListItemWillUseInstantBackCompletionHandler : 1;

#if HAVE(APP_SSO)
        bool webViewDecidePolicyForSOAuthorizationLoadWithCurrentPolicyForExtensionCompletionHandler : 1;
#endif
        bool webViewDidGeneratePageLoadTiming : 1;
    } m_navigationDelegateMethods;

    WeakObjCPtr<id<WKHistoryDelegatePrivate>> m_historyDelegate;
    struct {
        bool webViewDidNavigateWithNavigationData : 1;
        bool webViewDidPerformClientRedirectFromURLToURL : 1;
        bool webViewDidPerformServerRedirectFromURLToURL : 1;
        bool webViewDidUpdateHistoryTitleForURL : 1;
    } m_historyDelegateMethods;

#if USE(RUNNINGBOARD)
    RefPtr<ProcessThrottler::BackgroundActivity> m_networkActivity;
    RunLoop::Timer m_releaseNetworkActivityTimer;
#endif
};

} // namespace WebKit
