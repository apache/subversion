#
# Copyright (C) 2000-2002 The ViewCVS Group. All Rights Reserved.
#
# By using this file, you agree to the terms and conditions set forth in
# the LICENSE.html file which can be found at the top level of the ViewCVS
# distribution or at http://viewcvs.sourceforge.net/license-1.html.
#
# Contact information:
#   Greg Stein, PO Box 760, Palo Alto, CA, 94302
#   gstein@lyra.org, http://viewcvs.sourceforge.net/
#
# -----------------------------------------------------------------------
#
# This software is being maintained as part of the ViewCVS project.
# Information is available at:
#    http://viewcvs.sourceforge.net/
#
# -----------------------------------------------------------------------

"""This package provides parsing tools for RCS files."""

from common import *

try:
  from tparse import parse
except ImportError:
  try:
    from texttools import Parser
  except ImportError:
    from default import Parser

  def parse(file, sink):
    return Parser().parse(file, sink)
