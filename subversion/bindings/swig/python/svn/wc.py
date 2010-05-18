#
# wc.py: public Python interface for wc components
#
# Subversion is a tool for revision control.
# See http://subversion.tigris.org for more information.
#
######################################################################
#
# Copyright (c) 2000-2004 CollabNet.  All rights reserved.
#
# This software is licensed as described in the file COPYING, which
# you should have received as part of this distribution.  The terms
# are also available at http://subversion.tigris.org/license-1.html.
# If newer versions of this license are posted there, you may use a
# newer version instead, at your option.
#
######################################################################

from libsvn.wc import *
from svn.core import _unprefix_names
_unprefix_names(locals(), 'svn_wc_')
_unprefix_names(locals(), 'SVN_WC_')
del _unprefix_names


class DiffCallbacks2:
    def file_changed(self, adm_access, path,
                     tmpfile1, tmpfile2, rev1, rev2,
                     mimetype1, mimetype2,
                     propchanges, originalprops):
        return (notify_state_unknown, notify_state_unknown)

    def file_added(self, adm_access, path,
                   tmpfile1, tmpfile2, rev1, rev2,
                   mimetype1, mimetype2,
                   propchanges, originalprops):
        return (notify_state_unknown, notify_state_unknown)

    def file_deleted(self, adm_access, path, tmpfile1, tmpfile2,
                     mimetype1, mimetype2, originalprops):
        return notify_state_unknown

    def dir_added(self, adm_access, path, rev):
        return notify_state_unknown

    def dir_deleted(self, adm_access, path):
        return notify_state_unknown

    def dir_props_changed(self, adm_access, path,
                          propchanges, original_props):
        return notify_state_unknown
