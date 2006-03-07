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
public class ChangePath
{
    /**
     * Constructor to be called from the native code
     * @param path              path of the commit item
     * @param copySrcRevision   copy source revision (if any)
     * @param copySrcPath       copy source path (if any)
     * @param action            action performed
     */
    ChangePath(String path, long copySrcRevision, String copySrcPath, char action)
    {
        this.path = path;
        this.copySrcRevision = copySrcRevision;
        this.copySrcPath = copySrcPath;
        this.action = action;
    }

    /** Path of commited item */
    private String path;

    /** Source revision of copy (if any). */
    private long copySrcRevision;

    /** Source path of copy (if any). */
    private String copySrcPath;

    /** 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
    private char action;

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
}
