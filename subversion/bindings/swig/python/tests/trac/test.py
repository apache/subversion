#!/usr/bin/env python
# -*- coding: iso8859-1 -*-
#
# Copyright (C) 2003, 2004, 2005 Edgewall Software
# Copyright (C) 2003, 2004, 2005 Jonas Borgström <jonas@edgewall.com>
# Copyright (C) 2005 Christopher Lenz <cmlenz@gmx.de>
#
# This software is licensed as described in the file
# LICENSE_FOR_PYTHON_BINDINGS, which you should have received as part
# of this distribution.  The terms are also available at
# < http://subversion.tigris.org/license-for-python-bindings.html >.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# Author: Jonas Borgström <jonas@edgewall.com>
#         Christopher Lenz <cmlenz@gmx.de>

import unittest


class TestSetup(unittest.TestSuite):
    """
    Test suite decorator that allows a fixture to be setup for a complete
    suite of test cases.
    """
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def __call__(self, result):
        self.setUp()
        unittest.TestSuite.__call__(self, result)
        self.tearDown()
        return result
