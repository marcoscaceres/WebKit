/*
 * Copyright (C) 2015-2021 Apple Inc. All rights reserved.
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
#include "WebGLVertexArrayObjectBase.h"

#if ENABLE(WEBGL)

#include "WebCoreOpaqueRootInlines.h"
#include "WebGLRenderingContextBase.h"
#include <JavaScriptCore/AbstractSlotVisitorInlines.h>
#include <wtf/Locker.h>

namespace WebCore {

WebGLVertexArrayObjectBase::WebGLVertexArrayObjectBase(WebGLRenderingContextBase& context, PlatformGLObject object, Type type)
    : WebGLObject(context, object)
    , m_type(type)
{
    m_vertexAttribState.grow(context.maxVertexAttribs());
}

void WebGLVertexArrayObjectBase::setElementArrayBuffer(const AbstractLocker& locker, WebGLBuffer* buffer)
{
    if (buffer)
        buffer->onAttached();
    if (RefPtr boundElementArrayBuffer = m_boundElementArrayBuffer.get())
        boundElementArrayBuffer->onDetached(locker, context()->protectedGraphicsContextGL().get());
    m_boundElementArrayBuffer = buffer;
    
}
void WebGLVertexArrayObjectBase::setVertexAttribEnabled(int index, bool flag)
{
    auto& state = m_vertexAttribState[index];
    if (state.enabled == flag)
        return;
    state.enabled = flag;
    if (!state.validateBinding())
        m_allEnabledAttribBuffersBoundCache = false;
    else
        m_allEnabledAttribBuffersBoundCache.reset();
}

void WebGLVertexArrayObjectBase::setVertexAttribState(const AbstractLocker& locker, GCGLuint index, GCGLsizei bytesPerElement, GCGLint size, GCGLenum type, GCGLboolean normalized, GCGLsizei stride, GCGLintptr offset, bool isInteger, WebGLBuffer* buffer)
{
    auto& state = m_vertexAttribState[index];
    bool bindingWasValid = state.validateBinding();
    if (buffer)
        buffer->onAttached();
    if (RefPtr bufferBinding = state.bufferBinding.get())
        bufferBinding->onDetached(locker, context()->protectedGraphicsContextGL().get());
    state.bufferBinding = buffer;
    if (!state.validateBinding())
        m_allEnabledAttribBuffersBoundCache = false;
    else if (!bindingWasValid)
        m_allEnabledAttribBuffersBoundCache.reset();
    state.bytesPerElement = bytesPerElement;
    state.size = size;
    state.type = type;
    state.normalized = normalized;
    state.stride = stride ? stride : bytesPerElement;
    state.originalStride = stride;
    state.offset = offset;
    state.isInteger = isInteger;
}

void WebGLVertexArrayObjectBase::unbindBuffer(const AbstractLocker& locker, WebGLBuffer& buffer)
{
    if (RefPtr boundElementArrayBuffer = m_boundElementArrayBuffer.get(); boundElementArrayBuffer == &buffer) {
        boundElementArrayBuffer->onDetached(locker, context()->protectedGraphicsContextGL().get());
        m_boundElementArrayBuffer = nullptr;
    }
    
    for (auto& state : m_vertexAttribState) {
        if (state.bufferBinding == &buffer) {
            buffer.onDetached(locker, context()->protectedGraphicsContextGL().get());
            state.bufferBinding = nullptr;
            if (!state.validateBinding())
                m_allEnabledAttribBuffersBoundCache = false;
        }
    }
}

void WebGLVertexArrayObjectBase::setVertexAttribDivisor(GCGLuint index, GCGLuint divisor)
{
    m_vertexAttribState[index].divisor = divisor;
}

void WebGLVertexArrayObjectBase::addMembersToOpaqueRoots(const AbstractLocker&, JSC::AbstractSlotVisitor& visitor)
{
    SUPPRESS_UNCOUNTED_ARG addWebCoreOpaqueRoot(visitor, m_boundElementArrayBuffer.get());
    for (auto& state : m_vertexAttribState)
        SUPPRESS_UNCOUNTED_ARG addWebCoreOpaqueRoot(visitor, state.bufferBinding.get());
}

bool WebGLVertexArrayObjectBase::areAllEnabledAttribBuffersBound()
{
    if (!m_allEnabledAttribBuffersBoundCache) {
        m_allEnabledAttribBuffersBoundCache = [&] {
            for (const auto& state : m_vertexAttribState) {
                if (!state.validateBinding())
                    return false;
            }
            return true;
        }();
    }
    return *m_allEnabledAttribBuffersBoundCache;
}

WebCoreOpaqueRoot root(WebGLVertexArrayObjectBase* array)
{
    return WebCoreOpaqueRoot { array };
}

}

#endif // ENABLE(WEBGL)
