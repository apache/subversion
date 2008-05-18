/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 * @endcopyright
 */

package org.tigris.subversion.javahl;

import java.util.Map;

/**
 * This interface is used to receive every log message for the log
 * messages found by a SVNClientInterface.logMessages call.
 *
 * All log messages are returned in a list, which is terminated by an
 * invocation of this callback with the revision set to SVN_INVALID_REVNUM.
 *
 * If the includeMergedRevisions parameter to SVNClientInterface.logMessages
 * is true, then messages returned through this callback may have the
 * hasChildren parameter set.  This parameter indicates that a separate list,
 * which includes messages for merged revisions, will immediately follow.
 * This list is also terminated with SVN_INVALID_REVNUM, after which the
 * previous log message list continues.
 *
 * Log message lists may be nested arbitrarily deep, depending on the ancestry
 * of the requested paths.
 */
public interface LogMessageCallback
{
    /**
     * The method will be called for every log message.
     *
     * @param changedPaths   the paths that were changed
     * @param revision       the revision of the commit
     * @param revprops       All of the requested revision properties,
     *                       possibly including svn:date, svn:author,
     *                       and svn:log.
     * @param hasChildren    when merge sensitive option was requested,
     *                       whether or not this entry has child entries.
     */
    public void singleMessage(ChangePath[] changedPaths,
                              long revision,
                              Map revprops,
                              boolean hasChildren);
}
