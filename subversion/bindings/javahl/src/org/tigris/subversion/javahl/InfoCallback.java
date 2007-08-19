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
 * This interface is used to retrieve info each result in a
 * SVNClientInterface.info2 call.
 */
public interface InfoCallback
{
    /**
     * the method will be called for every line in a file.
     * @param info      the Info2 object
     */
    public void singleInfo(Info2 info);
}
