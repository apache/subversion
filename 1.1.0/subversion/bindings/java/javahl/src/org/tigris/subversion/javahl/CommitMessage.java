/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * this is callback interface which has to implemented by the client to
 * receive which files will be commited and to enter the log message
 */
public interface CommitMessage
{
    /**
     * Retrieve a commit message from the user based on the items to be commited
     * @param elementsToBeCommited  Array of elements to be commited
     * @return  the log message of the commit.
     */
    String getLogMessage(CommitItem[] elementsToBeCommited);
}
