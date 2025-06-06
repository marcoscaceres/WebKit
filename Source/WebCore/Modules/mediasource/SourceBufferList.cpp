/*
 * Copyright (C) 2013 Google Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "SourceBufferList.h"

#if ENABLE(MEDIA_SOURCE)

#include "ContextDestructionObserverInlines.h"
#include "Event.h"
#include "EventNames.h"
#include "SourceBuffer.h"
#include "WebCoreOpaqueRoot.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(SourceBufferList);

Ref<SourceBufferList> SourceBufferList::create(ScriptExecutionContext* context)
{
    auto result = adoptRef(*new SourceBufferList(context));
    result->suspendIfNeeded();
    return result;
}

SourceBufferList::SourceBufferList(ScriptExecutionContext* context)
    : ActiveDOMObject(context)
{
}

SourceBufferList::~SourceBufferList()
{
    ASSERT(m_list.isEmpty());
}

void SourceBufferList::add(Ref<SourceBuffer>&& buffer)
{
    m_list.append(WTFMove(buffer));
    scheduleEvent(eventNames().addsourcebufferEvent);
}

bool SourceBufferList::contains(SourceBuffer& buffer) const
{
    return m_list.find(Ref { buffer } ) != notFound;
}

RefPtr<SourceBuffer> SourceBufferList::item(unsigned index) const
{
    if (index < m_list.size())
        return m_list[index].copyRef();
    return { };
}

void SourceBufferList::remove(SourceBuffer& buffer)
{
    size_t index = m_list.find(Ref { buffer });
    if (index == notFound)
        return;
    m_list.removeAt(index);
    scheduleEvent(eventNames().removesourcebufferEvent);
}

void SourceBufferList::clear()
{
    m_list.clear();
    scheduleEvent(eventNames().removesourcebufferEvent);
}

void SourceBufferList::replaceWith(Vector<Ref<SourceBuffer>>&& other)
{
    int changeInSize = other.size() - m_list.size();
    int addedEntries = 0;
    for (auto& sourceBuffer : other) {
        if (!m_list.contains(sourceBuffer))
            ++addedEntries;
    }
    int removedEntries = addedEntries - changeInSize;

    m_list = WTFMove(other);

    if (addedEntries)
        scheduleEvent(eventNames().addsourcebufferEvent);
    if (removedEntries)
        scheduleEvent(eventNames().removesourcebufferEvent);
}

void SourceBufferList::scheduleEvent(const AtomString& eventName)
{
    queueTaskToDispatchEvent(*this, TaskSource::MediaElement, Event::create(eventName, Event::CanBubble::No, Event::IsCancelable::No));
}

WebCoreOpaqueRoot root(SourceBufferList* list)
{
    return WebCoreOpaqueRoot { list };
}

} // namespace WebCore

#endif
