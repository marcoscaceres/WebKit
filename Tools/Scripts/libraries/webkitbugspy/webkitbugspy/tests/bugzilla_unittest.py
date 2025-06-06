# Copyright (C) 2021-2023 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import json
import logging
import re
import unittest
from unittest.mock import patch

from webkitcorepy import OutputCapture
from webkitcorepy import mocks as wkmocks

from webkitbugspy import Tracker, User, bugzilla, mocks, radar


class TestBugzilla(unittest.TestCase):
    URL = 'https://bugs.example.com'

    def test_encoding(self):
        self.assertEqual(
            bugzilla.Tracker.Encoder().default(bugzilla.Tracker(
                self.URL,
                res=[re.compile(r'\Aexample.com/b/(?P<id>\d+)\Z')],
            )), dict(
                type='bugzilla',
                url='https://bugs.example.com',
                hide_title=False,
                res=['\\Aexample.com/b/(?P<id>\\d+)\\Z']
            ),
        )

    def test_decoding(self):
        decoded = Tracker.from_json(json.dumps(bugzilla.Tracker(
            self.URL,
            res=[re.compile(r'\Aexample.com/b/(?P<id>\d+)\Z')],
        ), cls=Tracker.Encoder))
        self.assertIsInstance(decoded, bugzilla.Tracker)
        self.assertEqual(decoded.url, 'https://bugs.example.com')
        self.assertEqual(decoded.from_string('example.com/b/1234').id, 1234)

    def test_no_users(self):
        with mocks.Bugzilla(self.URL.split('://')[1]):
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).user(
                    name='Tim Contributor', username='tcontributor@example.com', email='tcontributor@example.com',
                )), dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).user(username='tcontributor@example.com')),
                dict(username='tcontributor@example.com'),
            )
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).user(email='tcontributor@example.com')),
                dict(emails=['tcontributor@example.com']),
            )

    def test_users(self):
        with mocks.Bugzilla(self.URL.split('://')[1], users=mocks.USERS):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(
                User.Encoder().default(tracker.user(username='tcontributor@example.com')),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            self.assertEqual(
                User.Encoder().default(tracker.user(name='Tim Contributor')),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            self.assertEqual(
                User.Encoder().default(tracker.user(email='ffiler@example.com')),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )

    def test_link(self):
        tracker = bugzilla.Tracker(self.URL)
        self.assertEqual(tracker.issue(1234).link, 'https://bugs.example.com/show_bug.cgi?id=1234')
        self.assertEqual(
            tracker.from_string('http://bugs.example.com/show_bug.cgi?id=1234').link,
            'https://bugs.example.com/show_bug.cgi?id=1234',
        )
        self.assertEqual(tracker.from_string('http://bugs.other.com/show_bug.cgi?id=1234'), None)

    def test_link_brackets(self):
        tracker = bugzilla.Tracker(self.URL)
        self.assertEqual(
            tracker.from_string('<http://bugs.example.com/show_bug.cgi?id=1234>').link,
            'https://bugs.example.com/show_bug.cgi?id=1234',
        )

    def test_title(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).title, 'Example issue 1')
            self.assertEqual(str(tracker.issue(1)), 'https://bugs.example.com/show_bug.cgi?id=1 Example issue 1')

    def test_timestamp(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(bugzilla.Tracker(self.URL).issue(1).timestamp, 1639510960)

    def test_modified(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(bugzilla.Tracker(self.URL).issue(1).modified, 1710859207)

    def test_creator(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).issue(1).creator),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )

    def test_description(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(
                bugzilla.Tracker(self.URL).issue(1).description,
                'An example issue for testing',
            )

    def test_assignee(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).issue(1).assignee),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )

    def test_comments(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            comments = bugzilla.Tracker(self.URL).issue(1).comments
            self.assertEqual(len(comments), 2)
            self.assertEqual(comments[0].timestamp, 1639511020)
            self.assertEqual(comments[0].content, 'Was able to reproduce on version 1.2.3')
            self.assertEqual(
                User.Encoder().default(comments[0].user),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )

    def test_watchers(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).issue(1).watchers), [
                    dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
                    dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
                    dict(name='Wilma Watcher', username='wwatcher@example.com', emails=['wwatcher@example.com']),
                ],
            )

    def test_references(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).references, [])
            self.assertEqual(tracker.issue(2).references, [tracker.issue(3)])
            self.assertEqual(tracker.issue(3).references, [tracker.issue(2)])

    def test_reference_parse(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='wwatcher@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            tracker = bugzilla.Tracker(self.URL)
            tracker.issue(1).add_comment('Is this related to {}/show_bug.cgi?id=2?'.format(self.URL))
            self.assertEqual(tracker.issue(1).references, [])

    def test_reference_order(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='wwatcher@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            tracker = bugzilla.Tracker(self.URL)
            tracker.issue(1).add_comment('{}/show_bug.cgi?id=2?'.format(self.URL))
            tracker.issue(1).add_comment('{}/show_bug.cgi?id=3?'.format(self.URL))
            self.assertEqual(tracker.issue(1).references, [tracker.issue(3), tracker.issue(2)])

    def test_reference_multiline(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='wwatcher@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            tracker = bugzilla.Tracker(self.URL)
            tracker.issue(1).add_comment(f'{self.URL}/show_bug.cgi?id=2 is a bug.\n{self.URL}/show_bug.cgi?id=3 is another bug.')
            self.assertEqual(tracker.issue(1).references, [tracker.issue(2), tracker.issue(3)])

            tracker.issue(1).add_comment(f'{self.URL}/show_bug.cgi?id=4')
            self.assertEqual(tracker.issue(1).references, [tracker.issue(4), tracker.issue(2), tracker.issue(3)])

    def test_me(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            self.assertEqual(
                User.Encoder().default(bugzilla.Tracker(self.URL).me()),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )

    def test_add_comment(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(len(issue.comments), 2)

            comment = issue.add_comment('Automated comment')
            self.assertEqual(comment.content, 'Automated comment')
            self.assertEqual(
                User.Encoder().default(comment.user),
                User.Encoder().default(bugzilla.Tracker(self.URL).me()),
            )

            self.assertEqual(len(issue.comments), 3)
            self.assertEqual(len(bugzilla.Tracker(self.URL).issue(1).comments), 3)

    def test_assign(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='ffiler@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(
                User.Encoder().default(issue.assignee),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            issue.assign(bugzilla.Tracker(self.URL).me())
            self.assertEqual(
                User.Encoder().default(issue.assignee),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )

            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(
                User.Encoder().default(issue.assignee),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )

    def test_assign_why(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='ffiler@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(
                User.Encoder().default(issue.assignee),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            issue.assign(bugzilla.Tracker(self.URL).me(), why='Let me provide a better reproduction')
            self.assertEqual(
                User.Encoder().default(issue.assignee),
                dict(name='Felix Filer', username='ffiler@example.com', emails=['ffiler@example.com']),
            )
            self.assertEqual(issue.comments[-1].content, 'Let me provide a better reproduction')

    def test_state(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertTrue(issue.opened)
            self.assertFalse(issue.open())
            self.assertTrue(issue.close())
            self.assertFalse(issue.opened)

            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertFalse(issue.opened)
            self.assertFalse(issue.close())
            self.assertTrue(issue.open())
            self.assertTrue(issue.opened)

    def test_state_why(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertTrue(issue.opened)
            self.assertTrue(issue.close(why='Fixed in 1234@main'))
            self.assertFalse(issue.opened)
            self.assertEqual(issue.comments[-1].content, 'Fixed in 1234@main')

            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertFalse(issue.opened)
            self.assertTrue(issue.open(why='Need to revert, fix broke the build'))
            self.assertTrue(issue.opened)
            self.assertEqual(issue.comments[-1].content, 'Need to revert, fix broke the build')

    def test_set_state(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        )), OutputCapture() as captured:
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertFalse(issue.set_state(state='Analyze'))
            self.assertEqual(issue.state, None)
        self.assertEqual(
            captured.stderr.getvalue(),
            'Bugzilla does not support state at this time\n',
        )

    def test_set_substate(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        )), OutputCapture() as captured:
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertFalse(issue.set_state(state='Analyze', substate='Fix'))
            self.assertEqual(issue.state, None)
        self.assertEqual(
            captured.stderr.getvalue(),
            'Bugzilla does not support state at this time\n',
        )

    def test_duplicate(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        )):
            tracker = bugzilla.Tracker(self.URL)
            issue = tracker.issue(1)
            self.assertTrue(issue.opened)
            self.assertTrue(issue.close(original=tracker.issue(2)))
            self.assertFalse(issue.opened)
            self.assertEqual(issue.original, tracker.issue(2))

            self.assertEqual(tracker.issue(1).original, tracker.issue(2))

    def test_projects(self):
        with mocks.Bugzilla(self.URL.split('://')[1], projects=mocks.PROJECTS):
            self.assertDictEqual(
                dict(
                    CFNetwork=dict(
                        description='Darwin networking framework',
                        versions=['All'],
                        components=dict(
                            IPv4=dict(description='Bugs involving IPv4 networking'),
                            IPv6=dict(description='Bugs involving IPv6 networking'),
                        ),
                    ), WebKit=dict(
                        description='The WebKit browser engine',
                        versions=['Other', 'Safari 15', 'Safari Technology Preview', 'WebKit Local Build'],
                        components=dict(
                            Scrolling=dict(description='Bugs related to main thread and off-main thread scrolling'),
                            SVG=dict(description='For bugs in the SVG implementation.'),
                            Tables=dict(description='For bugs specific to tables (both the DOM and rendering issues).'),
                            Text=dict(description='For bugs in text layout and rendering, including international text support.'),
                        ),
                    ),
                ), bugzilla.Tracker(self.URL).projects,
            )

    def test_create(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS, issues=mocks.ISSUES):
            created = bugzilla.Tracker(self.URL).create(
                'New bug', 'Creating new bug',
                project='WebKit', component='Tables', version='Other', keywords=['InRadar']
            )
            self.assertEqual(created.id, 4)
            self.assertEqual(created.title, 'New bug')
            self.assertEqual(created.description, 'Creating new bug')
            self.assertTrue(created.opened)
            self.assertEqual(
                User.Encoder().default(created.creator),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            self.assertEqual(
                User.Encoder().default(created.assignee),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )

            self.assertEqual(created.project, 'WebKit')
            self.assertEqual(created.component, 'Tables')
            self.assertEqual(created.version, 'Other')
            self.assertEqual(created.keywords, ['InRadar'])

    def test_create_prompt(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS, issues=mocks.ISSUES), wkmocks.Terminal.input('2', '1', '2'), OutputCapture() as captured:
            created = bugzilla.Tracker(self.URL).create('New bug', 'Creating new bug')
            self.assertEqual(created.id, 4)
            self.assertEqual(created.title, 'New bug')
            self.assertEqual(created.description, 'Creating new bug')
            self.assertTrue(created.opened)
            self.assertEqual(
                User.Encoder().default(created.creator),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )
            self.assertEqual(
                User.Encoder().default(created.assignee),
                dict(name='Tim Contributor', username='tcontributor@example.com', emails=['tcontributor@example.com']),
            )

            self.assertEqual(created.project, 'WebKit')
            self.assertEqual(created.component, 'SVG')

        self.assertEqual(
            captured.stdout.getvalue(),
            '''What project should the bug be associated with?:
    1) CFNetwork
    2) WebKit
: 
What component in 'WebKit' should the bug be associated with?:
    1) SVG
    2) Scrolling
    3) Tables
    4) Text
: 
''',
        )

    def test_get_component(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, projects=mocks.PROJECTS):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(issue.project, 'WebKit')
            self.assertEqual(issue.component, 'Text')
            self.assertEqual(issue.version, 'Other')

    def test_invalid_keyword(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS, issues=mocks.ISSUES):

            with self.assertRaises(ValueError) as e:
                bugzilla.Tracker(self.URL).create(
                    'New bug', 'Creating new bug',
                    project='WebKit', component='Tables', version='Other', keywords=['InvalidKeyword']
                )
            self.assertEqual(f"'InvalidKeyword' is not a valid keyword for 'WebKit'", str(e.exception))

    def test_set_component(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS, issues=mocks.ISSUES):
            bugzilla.Tracker(self.URL).issue(1).set_component(project='WebKit', component='Tables', version='Safari 15')

            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(issue.project, 'WebKit')
            self.assertEqual(issue.component, 'Tables')
            self.assertEqual(issue.version, 'Safari 15')

    def test_labels(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, projects=mocks.PROJECTS):
            issue = bugzilla.Tracker(self.URL).issue(1)
            self.assertEqual(issue.labels, [])

    def test_exhausted_logins(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS, issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            tracker._logins_left = 0

            with OutputCapture() as captured:
                self.assertFalse(tracker.issue(1).close())
            self.assertEqual(
                captured.stderr.getvalue(),
                'Exhausted login attempts\n'
                "Failed to modify 'https://bugs.example.com/show_bug.cgi?id=1 Example issue 1'\n",
            )

            with OutputCapture() as captured:
                self.assertIsNone(tracker.issue(1).add_comment('Failed comment'))
            self.assertEqual(
                captured.stderr.getvalue(),
                'Exhausted login attempts\n'
                "Failed to add comment to 'https://bugs.example.com/show_bug.cgi?id=1 Example issue 1'\n",
            )

            with OutputCapture() as captured:
                self.assertIsNone(tracker.create(
                    'New bug', 'Creating new bug',
                    project='WebKit', component='Tables', version='Other',
                ))
            self.assertEqual(
                captured.stderr.getvalue(),
                'Exhausted login attempts\n'
                'Failed to create bug: Login attempts exhausted\n',
            )

    def test_redaction(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, projects=mocks.PROJECTS):
            self.assertEqual(bugzilla.Tracker(self.URL, redact=None).issue(1).redacted, False)
            self.assertTrue(bool(bugzilla.Tracker(self.URL, redact={'.*': True}).issue(1).redacted))
            self.assertEqual(
                bugzilla.Tracker(self.URL, redact={'.*': True}).issue(1).redacted,
                bugzilla.Tracker.Redaction(True, 'is a Bugzilla'),
            )
            self.assertEqual(
                bugzilla.Tracker(self.URL, redact={'project:WebKit': True}).issue(1).redacted,
                bugzilla.Tracker.Redaction(True, "matches 'project:WebKit'"),
            )
            self.assertEqual(
                bugzilla.Tracker(self.URL, redact={'component:Text': True}).issue(1).redacted,
                bugzilla.Tracker.Redaction(True, "matches 'component:Text'"),
            )
            self.assertEqual(
                bugzilla.Tracker(self.URL, redact={'version:Other': True}).issue(1).redacted,
                bugzilla.Tracker.Redaction(True, "matches 'version:Other'"),
            )

    def test_redacted_duplicate(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS):
            tracker = bugzilla.Tracker(self.URL, redact={'component:Text': True})
            self.assertEqual(tracker.issue(1).redacted, bugzilla.Tracker.Redaction(True, "matches 'component:Text'"))
            self.assertEqual(tracker.issue(2).redacted, False)
            tracker.issue(1).close(original=tracker.issue(2))
            self.assertEqual(tracker.issue(2).redacted, bugzilla.Tracker.Redaction(True, "is related to https://bugs.example.com/show_bug.cgi?id=1 which matches 'component:Text'"))

    def test_redacted_original(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), projects=mocks.PROJECTS):
            tracker = bugzilla.Tracker(self.URL, redact={'component:Text': True})
            self.assertEqual(tracker.issue(1).redacted, bugzilla.Tracker.Redaction(True, "matches 'component:Text'"))
            self.assertEqual(tracker.issue(2).redacted, False)
            tracker.issue(2).close(original=tracker.issue(1))
            self.assertEqual(tracker.issue(2).redacted, bugzilla.Tracker.Redaction(True, "is related to https://bugs.example.com/show_bug.cgi?id=1 which matches 'component:Text'"))

    def test_redaction_exception(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES, projects=mocks.PROJECTS):
            self.assertEqual(bugzilla.Tracker(
                self.URL,
                redact={'.*': True},
                redact_exemption={'component:Text': True},
            ).issue(1).redacted, bugzilla.Tracker.Redaction(
                exemption=True, reason="matches 'component:Text'",
            ))
            self.assertEqual(bugzilla.Tracker(
                self.URL,
                redact={'.*': True},
                redact_exemption={'component:Scrolling': True},
            ).issue(1).redacted, bugzilla.Tracker.Redaction(True, 'is a Bugzilla'))

    def test_cc_no_radar(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS):
            issue = bugzilla.Tracker(self.URL, radar_importer=mocks.USERS['Radar WebKit Bug Importer']).issue(1)
            self.assertEqual(issue.references, [])
            self.assertIsNone(issue.cc_radar(block=True))

    def test_cc_no_importer(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS), mocks.NoRadar():
            radar_tracker = radar.Tracker()
            bugzilla_tracker = bugzilla.Tracker(self.URL)

            with patch('webkitbugspy.Tracker._trackers', [radar_tracker, bugzilla_tracker]):
                issue = bugzilla_tracker.issue(1)
                self.assertEqual(issue.references, [])
                self.assertIsNone(issue.cc_radar(block=True))

    def test_cc_with_radar(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS), mocks.NoRadar():
            radar_tracker = radar.Tracker()
            bugzilla_tracker = bugzilla.Tracker(self.URL, radar_importer=mocks.USERS['Radar WebKit Bug Importer'])

            with patch('webkitbugspy.Tracker._trackers', [radar_tracker, bugzilla_tracker]):
                issue = bugzilla_tracker.issue(1)
                self.assertEqual(issue.references, [])
                self.assertIsNotNone(issue.cc_radar(block=True))
                self.assertEqual(len(issue.references), 1)
                self.assertEqual(issue.references[0].link, 'rdar://1')
                self.assertEqual(issue.references[0].title, None)

    def test_cc_with_radarclient(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS), mocks.Radar(
            users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS,
        ):
            radar_tracker = radar.Tracker()
            bugzilla_tracker = bugzilla.Tracker(self.URL, radar_importer=mocks.USERS['Radar WebKit Bug Importer'])

            with patch('webkitbugspy.Tracker._trackers', [radar_tracker, bugzilla_tracker]):
                issue = bugzilla_tracker.issue(1)
                self.assertEqual(issue.references, [])
                self.assertIsNotNone(issue.cc_radar(block=True))
                self.assertEqual(len(issue.references), 1)
                self.assertEqual(issue.references[0].link, 'rdar://4')
                self.assertEqual(issue.references[0].title, 'An example issue for testing (1)')

    def test_cc_existing_radar(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
            BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
            BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS), mocks.Radar(
            users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS,
        ), wkmocks.Time:
            radar_tracker = radar.Tracker()
            bugzilla_tracker = bugzilla.Tracker(self.URL, radar_importer=mocks.USERS['Radar WebKit Bug Importer'])

            with patch('webkitbugspy.Tracker._trackers', [radar_tracker, bugzilla_tracker]):
                issue = bugzilla_tracker.issue(1)
                self.assertEqual(issue.references, [])
                self.assertIsNotNone(issue.cc_radar(block=True, radar=Tracker.from_string('<rdar://1>')))
                self.assertEqual(issue.comments[-1].content, '<rdar://problem/1>')
                self.assertEqual(len(issue.references), 1)
                self.assertEqual(issue.references[0].link, 'rdar://1')
                self.assertEqual(issue.references[0].title, 'Example issue 1')

    def test_milestone(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).milestone, None)

    def test_keywords(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).keywords, ['Keyword A'])

    def test_classification(self):
        with mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).classification, '')

    def test_set_keywords(self):
        with OutputCapture(level=logging.INFO), mocks.Bugzilla(
            self.URL.split('://')[1],
            projects=mocks.PROJECTS,
            environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
            ), users=mocks.USERS, issues=mocks.ISSUES,
        ):

            tracker = bugzilla.Tracker(self.URL)
            issue = tracker.issue(1)

            self.assertEqual(issue.keywords, ['Keyword A'])
            issue.set_keywords(['REGRESSION'])
            self.assertEqual(issue.keywords, ['REGRESSION'])
            self.assertEqual(tracker.issue(1).keywords, ['REGRESSION'])

    def test_relate_simple(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            issue = tracker.issue(1)

            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})
            issue.relate(depends_on=tracker.issue(2))
            self.assertEqual(issue.related['depends_on'], [tracker.issue(2)])
            self.assertEqual(issue.related['blocks'], [])

    def test_relate(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            issue = tracker.issue(1)

            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})
            issue.relate(depends_on=tracker.issue(2), blocks=tracker.issue(3))
            self.assertEqual(issue.related['depends_on'], [tracker.issue(2)])
            self.assertEqual(issue.related['blocks'], [tracker.issue(3)])

    def test_relate_fail(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)

            issue = tracker.issue(1)

            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})
            with self.assertRaises(TypeError) as c:
                issue.relate(fake_relation=0)
            self.assertEqual('\'fake_relation\' is an invalid relation', str(c.exception))
            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})

    def test_relate_fail_type(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)

            issue = tracker.issue(1)

            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})
            with self.assertRaises(AttributeError) as c:
                issue.relate(blocks=17)
            self.assertEqual('\'int\' object has no attribute \'tracker\'', str(c.exception))
            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})

    def test_relate_fail_radar(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES), mocks.Radar(users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS,):
            bugzilla_tracker = bugzilla.Tracker(self.URL)
            radar_tracker = radar.Tracker()

            issue = bugzilla_tracker.issue(1)

            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})
            with self.assertRaises(TypeError) as c:
                issue.relate(blocks=radar_tracker.issue(1))
            self.assertEqual('Cannot relate issues of different types.', str(c.exception))
            self.assertEqual(issue.related, {'depends_on': [], 'blocks': [], 'regressions': [], 'regressed_by': []})

    def test_source_changes(self):
        with OutputCapture() as captured, mocks.Bugzilla(self.URL.split('://')[1], issues=mocks.ISSUES):
            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).source_changes, [])
            self.assertIsNone(tracker.issue(1).add_source_change('WebKit, merge, a4daad5b9fbd26d557088037f54dc0935a437182'))

        self.assertEqual(
            captured.stderr.getvalue(),
            'Bugzilla does not support source changes at this time\n',
        )

    def test_related_links(self):
        with mocks.Bugzilla(self.URL.split('://')[1], environment=wkmocks.Environment(
                BUGS_EXAMPLE_COM_USERNAME='tcontributor@example.com',
                BUGS_EXAMPLE_COM_PASSWORD='password',
        ), users=mocks.USERS, issues=mocks.ISSUES), mocks.Radar(users=mocks.USERS, issues=mocks.ISSUES, projects=mocks.PROJECTS,):

            tracker = bugzilla.Tracker(self.URL)
            self.assertEqual(tracker.issue(1).related_links, [])
            self.assertTrue(tracker.issue(1).add_related_links([f'{self.URL}/show_bug.cgi?id=2', 'other.tracker.com/123']))
            self.assertEqual(tracker.issue(1).related_links, [f'{self.URL}/show_bug.cgi?id=2', 'other.tracker.com/123'])
            self.assertEqual(tracker.issue(1).references, [tracker.issue(2)])
