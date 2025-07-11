# Copyright (C) 2008-2020 Andrey Petrov and contributors.
# Copyright (C) 2023 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import sys

if sys.version_info < (3, 9):  # noqa: UP036
    raise ImportError("webkitpy requires Python 3.9")

import os
import platform

# We always want the real system version
os.environ['SYSTEM_VERSION_COMPAT'] = '0'


libraries = os.path.join(os.path.abspath(os.path.dirname(os.path.dirname(__file__))), 'libraries')
webkitcorepy_path = os.path.join(libraries, 'webkitcorepy')
if webkitcorepy_path not in sys.path:
    sys.path.insert(0, webkitcorepy_path)
import webkitcorepy

if sys.platform == 'darwin':
    is_root = not os.getuid()
    does_own_libraries = os.stat(libraries).st_uid == os.getuid()
    directory_writeable = os.access(libraries, os.W_OK)
    if any([is_root, not does_own_libraries, not directory_writeable]):
        libraries = os.path.expanduser('~/Library/webkitpy')

from webkitcorepy import AutoInstall, Package, Version
AutoInstall.set_directory(os.path.join(libraries, 'autoinstalled', 'python-{}-{}'.format(sys.version_info[0], platform.machine())))

AutoInstall.register(Package('pylint', Version(2, 13, 9)))
AutoInstall.register(
    Package("pytest", Version(7, 2, 0),
            implicit_deps=["attr", "pluggy", "iniconfig"]
            + (["exceptiongroup"] if sys.version_info < (3, 11) else []),
            aliases=["_pytest"]  # Technically also a stub "py" module, but this conflicts with the py project.
            )
)
AutoInstall.register(Package('pytest_asyncio', Version(0, 18, 3), pypi_name='pytest-asyncio', implicit_deps=['pytest'], wheel=True))
AutoInstall.register(Package('pytest_timeout', Version(2, 1, 0), pypi_name='pytest-timeout', implicit_deps=['pytest'], wheel=True))
AutoInstall.register(Package('websockets', Version(12, 0), wheel=True))

if sys.version_info < (3, 11):
    AutoInstall.register(Package('exceptiongroup', Version(1, 1, 0), wheel=True))

AutoInstall.register(Package('importlib_metadata', Version(4, 8, 1)))
AutoInstall.register(Package('typing_extensions', Version(4, 12, 2), wheel=True))
AutoInstall.register(Package('atomicwrites', Version(1, 1, 5)))
AutoInstall.register(Package('attrs', Version(21, 3, 0), aliases=['attr']))
AutoInstall.register(Package('bs4', Version(4, 12, 0), pypi_name='beautifulsoup4'))
AutoInstall.register(Package('configparser', Version(4, 0, 2), implicit_deps=['pyparsing'], aliases=['backports.configparser']))
AutoInstall.register(Package('contextlib2', Version(0, 6, 0)))
AutoInstall.register(Package('coverage', Version(7, 6, 1), wheel=True))
AutoInstall.register(Package('funcsigs', Version(1, 0, 2)))
AutoInstall.register(Package('html5lib', Version(1, 1)))
AutoInstall.register(Package('iniconfig', Version(1, 1, 1)))
AutoInstall.register(Package('mechanize', Version(0, 4, 5)))
AutoInstall.register(Package('more_itertools', Version(4, 2, 0), pypi_name='more-itertools', wheel=True))
AutoInstall.register(Package('mozprocess', Version(1, 3, 0)))
AutoInstall.register(Package('mozlog', Version(7, 1, 0), wheel=True))
AutoInstall.register(Package('mozterm', Version(1, 0, 0)))
AutoInstall.register(Package('pluggy', Version(0, 13, 1)))
AutoInstall.register(Package('pycodestyle', Version(2, 5, 0)))
AutoInstall.register(Package('pyfakefs', Version(5, 7, 3)))
AutoInstall.register(Package('soupsieve', Version(2, 2, 1)))

if sys.platform == 'linux':
    # Keep websocket toplevel for WebDriverTests' imported selenium
    AutoInstall.register(Package('websocket', Version(1, 8, 0), pypi_name='websocket-client'))
    AutoInstall.register(Package('selenium', Version(4, 24, 0), wheel=True, implicit_deps=['websocket']))
else:
    AutoInstall.register(Package('selenium', Version(4, 12, 0), wheel=True))

AutoInstall.register(Package('toml', Version(0, 10, 1), implicit_deps=['pyparsing']))
AutoInstall.register(Package('wcwidth', Version(0, 2, 5)))
AutoInstall.register(Package('webencodings', Version(0, 5, 1)))
AutoInstall.register(Package('zipp', Version(1, 2, 0)))
AutoInstall.register(Package('zope.interface', Version(7, 0, 1), aliases=['zope'], pypi_name='zope-interface', wheel=True))
AutoInstall.register(Package('webkitscmpy', Version(4, 0, 0)), local=True)
AutoInstall.register(Package('webkitbugspy', Version(0, 3, 1)), local=True)
AutoInstall.register(Package('yaml', Version(6, 0, 2), pypi_name='PyYAML'))

import webkitscmpy

# Disable IPV6 if Bugzilla can't use IPV6.
try:
    import socket
    import urllib3

    HAS_CHECKED_IPV6 = False
    FIRST_IPV6_TIMEOUT = 3

    # Pulled from urllib3, https://github.com/urllib3/urllib3/blob/main/src/urllib3/util/connection.py
    # Disable IPV6 if the first attempt to connect to an IPV6 address fails
    def create_connection(
        address,
        timeout=socket._GLOBAL_DEFAULT_TIMEOUT,
        source_address=None,
        socket_options=None,
    ):
        global HAS_CHECKED_IPV6

        host, port = address
        if host.startswith("["):
            host = host.strip("[]")
        err = None

        family = urllib3.util.connection.allowed_gai_family()

        try:
            host.encode("idna")
        except UnicodeError as e:
            raise LocationParseError("'%s', label empty or too long" % host) from e

        for res in socket.getaddrinfo(host, port, family, socket.SOCK_STREAM):
            af, socktype, proto, canonname, sa = res

            is_first_ipv6 = not HAS_CHECKED_IPV6 and af == socket.AF_INET6
            if af == socket.AF_INET6 and not urllib3.util.connection.HAS_IPV6:
                continue

            sock = None
            try:
                sock = socket.socket(af, socktype, proto)

                urllib3.util.connection._set_socket_options(sock, socket_options)

                if is_first_ipv6 and (not timeout or timeout > FIRST_IPV6_TIMEOUT):
                    sock.settimeout(FIRST_IPV6_TIMEOUT)
                elif timeout is not socket._GLOBAL_DEFAULT_TIMEOUT:
                    sock.settimeout(timeout)
                if source_address:
                    sock.bind(source_address)
                sock.connect(sa)
                return sock

            except socket.error as e:
                err = e
                if sock is not None:
                    sock.close()
                    sock = None
                if is_first_ipv6:
                    sys.stderr.write('IPV6 request to {} has failed, disabling IPV6\n'.format(host))
                    urllib3.util.connection.HAS_IPV6 = False
            finally:
                if af == socket.AF_INET6:
                    HAS_CHECKED_IPV6 = True

        if err is not None:
            raise err

        raise socket.error("getaddrinfo returns an empty list")

    urllib3.util.connection.create_connection = create_connection

except ModuleNotFoundError:
    pass

except ImportError:
    pass
