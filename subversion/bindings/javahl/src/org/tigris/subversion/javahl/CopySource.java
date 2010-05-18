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
 * A description of a copy source.
 */
public class CopySource implements java.io.Serializable
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
     * The source path or URL.
     */
    private String path;

    /**
     * The source revision.
     */
    private Revision revision;

    /**
     * The peg revision.
     */
    private Revision pegRevision;

    /**
     * Create a new instance.
     *
     * @param path
     * @param revision The source revision.
     * @param pegRevision The peg revision.  Typically interpreted as
     * {@link org.tigris.subversion.javahl.Revision#HEAD} when
     * <code>null</code>.
     */
    public CopySource(String path, Revision revision, Revision pegRevision)
    {
        this.path = path;
        this.revision = revision;
        this.pegRevision = pegRevision;
    }

    /**
     * @return The source path or URL.
     */
    public String getPath()
    {
        return this.path;
    }

    /**
     * @return The source revision.
     */
    public Revision getRevision()
    {
        return this.revision;
    }

    /**
     * @return The peg revision.
     */
    public Revision getPegRevision()
    {
        return this.pegRevision;
    }
}
