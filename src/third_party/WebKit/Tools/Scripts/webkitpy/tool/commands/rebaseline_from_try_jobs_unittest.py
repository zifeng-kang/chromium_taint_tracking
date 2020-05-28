# Copyright 2016 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import json

from webkitpy.common.net.buildbot import Build
from webkitpy.common.net.web_mock import MockWeb
from webkitpy.common.system.outputcapture import OutputCapture
from webkitpy.layout_tests.builder_list import BuilderList
from webkitpy.tool.commands.rebaseline_from_try_jobs import RebaselineFromTryJobs
from webkitpy.tool.commands.rebaseline_unittest import BaseTestCase
from webkitpy.tool.mock_tool import MockOptions
from webkitpy.common.checkout.scm.scm_mock import MockSCM


class RebaselineFromTryJobsTest(BaseTestCase):
    command_constructor = RebaselineFromTryJobs

    def setUp(self):
        super(RebaselineFromTryJobsTest, self).setUp()
        self.command.web = MockWeb(urls={
            'https://codereview.chromium.org/api/11112222': json.dumps({
                'patchsets': [1, 2],
            }),
            'https://codereview.chromium.org/api/11112222/2': json.dumps({
                'try_job_results': [
                    {
                        'builder': 'MOCK Try Win',
                        'buildnumber': 5000,
                    },
                    {
                        'builder': 'MOCK Mac Try',
                        'buildnumber': 4000,
                    },
                ],
            }),
        })
        self.tool.builders = BuilderList({
            "MOCK Try Win": {
                "port_name": "test-win-win7",
                "specifiers": ["Win7", "Release"],
                "is_try_builder": True,
            },
            "MOCK Try Mac": {
                "port_name": "test-mac-mac10.10",
                "specifiers": ["Mac10.10", "Release"],
                "is_try_builder": True,
            },
        })
        self.git = MockSCM()
        self.git.get_issue_number = lambda: 'None'
        self.command._git = lambda: self.git

    @staticmethod
    def command_options(**kwargs):
        options = {
            'issue': None,
            'dry_run': False,
            'optimize': True,
            'verbose': False,
            'results_directory': None,
        }
        options.update(kwargs)
        return MockOptions(**options)

    def test_execute_with_issue_number_given(self):
        oc = OutputCapture()
        try:
            oc.capture_output()
            self.command.execute(self.command_options(issue=11112222), [], self.tool)
        finally:
            _, _, logs = oc.restore_output()
        self.assertMultiLineEqual(
            logs,
            ('Tests to rebaseline:\n'
             '  svg/dynamic-updates/SVGFEDropShadowElement-dom-stdDeviation-attr.html: MOCK Try Win (5000)\n'
             '  fast/dom/prototype-inheritance.html: MOCK Try Win (5000)\n'
             '  fast/dom/prototype-taco.html: MOCK Try Win (5000)\n'
             'Rebaselining fast/dom/prototype-inheritance.html\n'
             'Rebaselining fast/dom/prototype-taco.html\n'
             'Rebaselining svg/dynamic-updates/SVGFEDropShadowElement-dom-stdDeviation-attr.html\n'))

    def test_execute_with_no_issue_number(self):
        oc = OutputCapture()
        try:
            oc.capture_output()
            self.command.execute(self.command_options(), [], self.tool)
        finally:
            _, _, logs = oc.restore_output()
        self.assertEqual(logs, 'No issue number given and no issue for current branch.\n')

    def test_execute_with_issue_number_from_branch(self):
        self.git.get_issue_number = lambda: '11112222'
        oc = OutputCapture()
        try:
            oc.capture_output()
            self.command.execute(self.command_options(), [], self.tool)
        finally:
            _, _, logs = oc.restore_output()
        self.assertMultiLineEqual(
            logs,
            ('Tests to rebaseline:\n'
             '  svg/dynamic-updates/SVGFEDropShadowElement-dom-stdDeviation-attr.html: MOCK Try Win (5000)\n'
             '  fast/dom/prototype-inheritance.html: MOCK Try Win (5000)\n'
             '  fast/dom/prototype-taco.html: MOCK Try Win (5000)\n'
             'Rebaselining fast/dom/prototype-inheritance.html\n'
             'Rebaselining fast/dom/prototype-taco.html\n'
             'Rebaselining svg/dynamic-updates/SVGFEDropShadowElement-dom-stdDeviation-attr.html\n'))

    def test_rebaseline_calls(self):
        """Tests the list of commands that are invoked when rebaseline is called."""
        # First write test contents to the mock filesystem so that
        # fast/dom/prototype-taco.html is considered a real test to rebaseline.
        # TODO(qyearsley): Change this to avoid accessing protected methods.
        # pylint: disable=protected-access
        port = self.tool.port_factory.get('test-win-win7')
        self._write(
            port._filesystem.join(port.layout_tests_dir(), 'fast/dom/prototype-taco.html'),
            'test contents')

        self.command._rebaseline(
            self.command_options(issue=11112222),
            {"fast/dom/prototype-taco.html": {Build("MOCK Try Win", 5000): ["txt", "png"]}})

        self.assertEqual(
            self.tool.executive.calls,
            [
                [['python', 'echo', 'copy-existing-baselines-internal', '--suffixes', 'txt',
                  '--builder', 'MOCK Try Win', '--test', 'fast/dom/prototype-taco.html', '--build-number', '5000']],
                [['python', 'echo', 'rebaseline-test-internal', '--suffixes', 'txt',
                  '--builder', 'MOCK Try Win', '--test', 'fast/dom/prototype-taco.html', '--build-number', '5000']],
                [['python', 'echo', 'optimize-baselines', '--no-modify-scm', '--suffixes', 'txt', 'fast/dom/prototype-taco.html']]
            ])
