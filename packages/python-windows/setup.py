#!/usr/bin/env python

# ====================================================================
# Copyright (c) 2006 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
# This software consists of voluntary contributions made by many
# individuals.  For exact contribution history, see the revision
# history and logs, available at http://subversion.tigris.org/.
# ====================================================================

from distutils.core import setup

setup (name = "svn-python",
       description = "Subversion Python Bindings",
       maintainer = "Subversion Developers <dev@subversion.tigris.org>",
       url = "http://subversion.tigris.org",
       version = "1.4.0",
       packages = ["libsvn", "svn"],
       package_data = {"libsvn": ["*.dll"]})
