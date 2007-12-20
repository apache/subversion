/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006-2007 CollabNet.  All rights reserved.
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
 * The event passed to the {@link
 * ProgressListener#onProgress(ProgressEvent)} API to inform {@link
 * SVNClientInterface} of command progress (in terms of bytes).
 *
 * @since 1.5
 */
public class ProgressEvent implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

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
