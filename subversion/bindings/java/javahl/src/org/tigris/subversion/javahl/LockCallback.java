/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * instance of this interface will be called during the SVNClientInterface.lock
 * and SVNClientInterface.unlock methods
 */
public interface LockCallback
{
    /**
     * this method is the callback
     * @param path      the item handled
     * @param doLock    if the operation is lock
     * @param lock      the lock attributes
     */
    public void lockNotify(String path, boolean doLock, Lock lock);
}
