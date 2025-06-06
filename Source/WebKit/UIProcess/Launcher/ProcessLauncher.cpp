/*
 * Copyright (C) 2010, 2012 Apple Inc. All rights reserved.
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
#include "ProcessLauncher.h"

#include <wtf/StdLibExtras.h>
#include <wtf/SystemTracing.h>
#include <wtf/WorkQueue.h>

#if OS(DARWIN)
#include <mach/mach_init.h>
#include <mach/mach_traps.h>
#endif

namespace WebKit {

ProcessLauncher::ProcessLauncher(Client* client, LaunchOptions&& launchOptions)
    : m_client(client)
    , m_launchOptions(WTFMove(launchOptions))
{
    tracePoint(ProcessLaunchStart, m_launchOptions.processIdentifier.toUInt64());
    launchProcess();
}

ProcessLauncher::~ProcessLauncher()
{
    platformDestroy();

    if (m_isLaunching)
        tracePoint(ProcessLaunchEnd, m_launchOptions.processIdentifier.toUInt64(), static_cast<uint64_t>(m_launchOptions.processType));
}

auto ProcessLauncher::checkedClient() const -> CheckedPtr<Client>
{
    return m_client;
}

#if !PLATFORM(COCOA)
void ProcessLauncher::platformDestroy()
{
}
#endif

void ProcessLauncher::didFinishLaunchingProcess(ProcessID processIdentifier, IPC::Connection::Identifier&& identifier)
{
    m_processID = processIdentifier;
    m_isLaunching = false;

    tracePoint(ProcessLaunchEnd, m_launchOptions.processIdentifier.toUInt64(), static_cast<uint64_t>(m_launchOptions.processType), static_cast<uint64_t>(m_processID));

    CheckedPtr client = m_client;
    if (!client) {
#if OS(DARWIN) && !USE(UNIX_DOMAIN_SOCKETS)
        // FIXME: Release port rights/connections in the Connection::Identifier destructor.
        if (identifier.port)
            mach_port_mod_refs(mach_task_self(), identifier.port, MACH_PORT_RIGHT_RECEIVE, -1);
#endif
        return;
    }

    client->didFinishLaunching(this, WTFMove(identifier));
}

void ProcessLauncher::invalidate()
{
    m_client = nullptr;
    platformInvalidate();
}

} // namespace WebKit
