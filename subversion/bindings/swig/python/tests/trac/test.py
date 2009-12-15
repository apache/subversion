#!/usr/bin/env python
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#
# -*- coding: utf-8 -*-
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
