/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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
 * The description of a merge conflict, encountered during
 * merge/update/switch operations.
 *
 * @since 1.6
 */
public class ConflictVersion
{
    private String reposURL;
    private long pegRevision;
    private String pathInRepos;

    /**
     * @see NodeKind
     */
    private int nodeKind;

    /** This constructor should only be called from JNI code. */
    ConflictVersion(String reposURL, long pegRevision, String pathInRepos,
                    int nodeKind)
    {
        this.reposURL = reposURL;
        this.pegRevision = pegRevision;
        this.pathInRepos = pathInRepos;
        this.nodeKind = nodeKind;
    }

    public String getReposURL()
    {
        return reposURL;
    }

    public long getPegRevision()
    {
        return pegRevision;
    }

    public String getPathInRepos()
    {
        return pathInRepos;
    }

    /**
     * @see NodeKind
     */
    public int getNodeKind()
    {
        return nodeKind;
    }

    public String toString() {
        return "(" + NodeKind.getNodeKindName(nodeKind) + ") " + reposURL +
        "/" + pathInRepos + "@" + pegRevision;
    }
}
