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
 * The concept of depth for directories.
 *
 * Note:
 * This is similar to, but not exactly the same as, the WebDAV and LDAP 
 * concepts of depth.
 *
 */
public final class Depth
{
    /** Depth undetermined or ignored. 
     * ### TODO(sd): If this gets removed in svn_types.h, remove it here. */
    public static final int unknown = -2;

    /** Exclude (remove, whatever) directory D.
     * ### TODO(sd): If this gets removed in svn_types.h, remove it here. */
    public static final int exclude = -1;

    /** Just the named directory D, no entries. */
    public static final int empty = 0;

    /** D + its file children, but not subdirs. */
    public static final int files = 1;

    /** D + immediate children (D and its entries). */
    public static final int immediates = 2;

    /** D + all descendants (full recursion from D). */
    public static final int infinity = 3;

    public static final int fromRecurse(boolean recurse)
    {
        if (recurse)
            return infinity;
        else
            return files;
    }
}
