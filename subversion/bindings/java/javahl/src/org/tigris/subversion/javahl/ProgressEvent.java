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

import java.util.EventObject;

/**
 * The event passed to the {@link
 * ProgressListener.onProgress(ProgressEvent)} API to inform {@link
 * SVNClientInterfacce} of command progress (in terms of bytes).
 *
 * @since 1.5
 */
public class ProgressEvent
{
    /**
     * The number of bytes already transferred.
     */
    private long progress = -1;

    /**
     * The total number of bytes, or <code>-1</code> if not known.
     */
    private long total = -1;

    /**
     * This constructor is to be used by the native code.
     *
     * @param progress The number of bytes already transferred.
     * @param total The total number of bytes, or <code>-1</code> if
     * not known.
     */
    ProgressEvent(long progress, long total)
    {
        this.progress = progress;
        this.total = total;
    }

    /**
     * @return The number of bytes already transferred.
     */
    public long getProgress()
    {
        return this.progress;
    }

    /**
     * @return The total number of bytes, or <code>-1</code> if not
     * known.
     */
    public long getTotal()
    {
        return this.total;
    }
}
