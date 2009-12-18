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

import java.util.Date;

/**
 * This interface is used to receive every single line for a file on a
 * the SVNClientInterface.blame call.
 */
public interface BlameCallback
{
    /**
     * the method will be called for every line in a file.
     * @param changed   the date of the last change.
     * @param revision  the revision of the last change.
     * @param author    the author of the last change.
     * @param line      the line in the file
     */
    public void singleLine(Date changed, long revision, String author,
                           String line);
}
