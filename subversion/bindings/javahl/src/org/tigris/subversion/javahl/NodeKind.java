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

/**
 * Poor mans enum for svn_node_kind_t
 */
public final class NodeKind
{
    /* absent */
    public static final int none = 0;

    /* regular file */
    public static final int file = 1;

    /* directory */
    public static final int dir = 2;

    /* something's here, but we don't know what */
    public static final int unknown = 3;

    /**
     * mapping for the constants to text
     */
    private static final String[] statusNames =
    {
        "none",
        "file",
        "dir ",
        "unknown",
    };

    /**
     * Returns the textual representation for a NodeKind
     * @param kind  kind of node
     * @return english text
     */
    public static final String getNodeKindName(int kind)
    {
        return statusNames[kind];
    }
}
