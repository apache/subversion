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

import java.util.Date;

/**
 * This interface is used to receive every single line for a file on a
 * the SVNClientInterface.blame call.
 *
 * @since 1.5
 */
public interface BlameCallback2
{
    /**
     * the method will be called for every line in a file.
     * @param date              the date of the last change.
     * @param revision          the revision of the last change.
     * @param author            the author of the last change.
     * @param merged_date       the date of the last merged change.
     * @param merged_revision   the revision of the last merged change.
     * @param merged_author     the author of the last merged change.
     * @param merged_path       the path of the last merged change.
     * @param line              the line in the file
     */
    public void singleLine(Date date, long revision, String author,
                           Date merged_date, long merged_revision,
                           String merged_author, String merged_path,
                           String line);
}
