/*
 * Copyright (C) 2020-2022 Apple Inc. All rights reserved.
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

#if ENABLE(GPU_PROCESS)

#include "IPCEvent.h"
#include "RemoteDisplayListRecorderIdentifier.h"
#include "ScopedActiveMessageReceiveQueue.h"
#include "ScopedRenderingResourcesRequest.h"
#include "StreamMessageReceiver.h"
#include <WebCore/ImageBuffer.h>
#include <WebCore/ShareableBitmap.h>

#if ENABLE(RE_DYNAMIC_CONTENT_SCALING)
#include <WebCore/DynamicContentScalingDisplayList.h>
#endif

namespace IPC {
class Semaphore;
class StreamConnectionWorkQueue;
}

namespace WebKit {

class RemoteDisplayListRecorder;
class RemoteRenderingBackend;

class RemoteImageBuffer : public IPC::StreamMessageReceiver {
public:
    static Ref<RemoteImageBuffer> create(Ref<WebCore::ImageBuffer>&&, WebCore::RenderingResourceIdentifier, RemoteDisplayListRecorderIdentifier, RemoteRenderingBackend&);
    ~RemoteImageBuffer();
    void stopListeningForIPC();
    Ref<WebCore::ImageBuffer> imageBuffer() const { return m_imageBuffer; }
    static Ref<WebCore::ImageBuffer> sinkIntoImageBuffer(Ref<RemoteImageBuffer>&&);
private:
    RemoteImageBuffer(Ref<WebCore::ImageBuffer>&&, WebCore::RenderingResourceIdentifier, RemoteDisplayListRecorderIdentifier, RemoteRenderingBackend&);
    void startListeningForIPC();
    IPC::StreamConnectionWorkQueue& workQueue() const;

    // IPC::StreamMessageReceiver
    void didReceiveStreamMessage(IPC::StreamServerConnection&, IPC::Decoder&) final;

    // Messages
    // This is using location and size as opposed to rect because invalid rect object gets restricted by IPC rect object decoder and triggers timeout in WebContent process.
    void getPixelBuffer(WebCore::PixelBufferFormat, WebCore::IntPoint srcPoint, WebCore::IntSize srcSize, CompletionHandler<void()>&&);
    void getPixelBufferWithNewMemory(WebCore::SharedMemory::Handle&&, WebCore::PixelBufferFormat, WebCore::IntPoint srcPoint, WebCore::IntSize srcSize, CompletionHandler<void()>&&);
    void putPixelBuffer(const WebCore::PixelBufferSourceView&, WebCore::IntPoint srcPoint, WebCore::IntSize srcSize, WebCore::IntPoint destPoint, WebCore::AlphaPremultiplication destFormat);
    void getShareableBitmap(WebCore::PreserveResolution, CompletionHandler<void(std::optional<WebCore::ShareableBitmap::Handle>&&)>&&);
    void filteredNativeImage(Ref<WebCore::Filter>, CompletionHandler<void(std::optional<WebCore::ShareableBitmap::Handle>&&)>&&);
    void convertToLuminanceMask();
    void transformToColorSpace(const WebCore::DestinationColorSpace&);
    void flushContext();
    void flushContextSync(CompletionHandler<void()>&&);

#if ENABLE(RE_DYNAMIC_CONTENT_SCALING)
    void dynamicContentScalingDisplayList(CompletionHandler<void(std::optional<WebCore::DynamicContentScalingDisplayList>&&)>&&);
#endif

    const Ref<WebCore::ImageBuffer> m_imageBuffer;
    const WebCore::RenderingResourceIdentifier m_identifier;
    const Ref<RemoteRenderingBackend> m_renderingBackend;
    IPC::ScopedActiveMessageReceiveQueue<RemoteDisplayListRecorder> m_context;
    ScopedRenderingResourcesRequest m_renderingResourcesRequest { ScopedRenderingResourcesRequest::acquire() };
};

} // namespace WebKit

#endif // ENABLE(GPU_PROCESS)
