/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

import java.util.EventListener;

/**
 * Subversion progress event interface.
 *
 * Implement this interface to provide a custom progress event handler
 * to the SVNClient class.  If you need to pass extra information to
 * the notification handler, add it to your implementing class.
 *
 * @since 1.5
 */
public interface ProgressListener extends EventListener
{
    /**
     * Implement this API to receive progress events for Subversion
     * operations.
     *
     * @param event everything to know about this event
     */
    public void onProgress(ProgressEvent event);
}
