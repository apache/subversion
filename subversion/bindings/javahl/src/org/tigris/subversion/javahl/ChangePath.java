/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004,2007 CollabNet.  All rights reserved.
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

public class ChangePath implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 2L;

    /**
     * Constructor to be called from the native code
     * @param path              path of the commit item
     * @param copySrcRevision   copy source revision (if any)
     * @param copySrcPath       copy source path (if any)
     * @param action            action performed
     * @param nodeKind          the kind of the changed path
     */
    ChangePath(String path, long copySrcRevision, String copySrcPath,
               char action, int nodeKind)
    {
        this.path = path;
        this.copySrcRevision = copySrcRevision;
        this.copySrcPath = copySrcPath;
        this.action = action;
        this.nodeKind = nodeKind;
    }

    /** Path of commited item */
    private String path;

    /** Source revision of copy (if any). */
    private long copySrcRevision;

    /** Source path of copy (if any). */
    private String copySrcPath;

    /** 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
    private char action;

    /** The kind of the changed path. */
    private int nodeKind;

    /**
     * Retrieve the path to the commited item
     * @return  the path to the commited item
     */
    public String getPath()
    {
        return path;
    }

    /**
     * Retrieve the copy source revision (if any)
     * @return  the copy source revision (if any)
     */
    public long getCopySrcRevision()
    {
        return copySrcRevision;
    }

    /**
     * Retrieve the copy source path (if any)
     * @return  the copy source path (if any)
     */
    public String getCopySrcPath()
    {
        return copySrcPath;
    }

    /**
     * Retrieve action performed
     * @return  action performed
     */
    public char getAction()
    {
        return action;
    }

    /**
     * Retrieve the node kind
     * @return  the node kind
     */
    public int getNodeKind()
    {
        return nodeKind;
    }
}
