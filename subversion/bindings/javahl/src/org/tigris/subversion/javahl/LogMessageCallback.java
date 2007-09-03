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

/**
 * This interface is used to receive every log message for the log
 * messages found by a SVNClientInterface.logMessages call.
 */
public interface LogMessageCallback
{
    /**
     * The method will be called for every log message.
     *
     * @param changedPaths   the paths that were changed
     * @param revision       the revision of the commit
     * @param author         the author of the commit
     * @param timeMicros     the time of the commit measured in the
     *                       number of microseconds since 00:00:00
     *                       January 1, 1970 UTC
     * @param message        the log message for the commit
     * @param hasChildren    when merge sensitive option was requested,
     *                       whether or not this entry has child entries.
     */
    public void singleMessage(ChangePath[] changedPaths,
                              long revision,
                              String author,
                              long timeMicros,
                              String message,
                              boolean hasChildren);
}
