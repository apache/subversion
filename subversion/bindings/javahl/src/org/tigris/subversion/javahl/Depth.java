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
    /* The order of these depths is important: the higher the number,
       the deeper it descends.  This allows us to compare two depths
       numerically to decide which should govern. */

    /** Depth undetermined or ignored. */
    public static final int unknown = -2;

    /** Exclude (i.e, don't descend into) directory D. */
    public static final int exclude = -1;

    /** Just the named directory D, no entries.  Updates will not pull in
        any files or subdirectories not already present. */
    public static final int empty = 0;

    /** D + its file children, but not subdirs.  Updates will pull in any
        files not already present, but not subdirectories. */
    public static final int files = 1;

    /** D + immediate children (D and its entries).  Updates will pull in
        any files or subdirectories not already present; those
        subdirectories' this_dir entries will have depth-empty. */
    public static final int immediates = 2;

    /** D + all descendants (full recursion from D).  Updates will pull
        in any files or subdirectories not already present; those
        subdirectories' this_dir entries will have depth-infinity.
        Equivalent to the pre-1.5 default update behavior. */
    public static final int infinity = 3;

    /**
     * @return A depth value of {@link #infinity} when
     * <code>recurse</code> is <code>true</code>, or {@link #empty}
     * otherwise.
     */
    public static final int infinityOrEmpty(boolean recurse)
    {
        return (recurse ? infinity : empty);
    }

    /**
     * @return A depth value of {@link #infinity} when
     * <code>recurse</code> is <code>true</code>, or {@link #files}
     * otherwise.
     */
    public static final int infinityOrFiles(boolean recurse)
    {
        return (recurse ? infinity : files);
    }

    /**
     * @return A depth value of {@link #infinity} when
     * <code>recurse</code> is <code>true</code>, or {@link
     * #immediates} otherwise.
     */
    public static final int infinityOrImmediates(boolean recurse)
    {
        return (recurse ? infinity : immediates);
    }

    /**
     * @return A depth value of {@link #unknown} when
     * <code>recurse</code> is <code>true</code>, or {@link #empty}
     * otherwise.
     */
    public static final int unknownOrEmpty(boolean recurse)
    {
        return (recurse ? unknown : empty);
    }

    /**
     * @return A depth value of {@link #unknown} when
     * <code>recurse</code> is <code>true</code>, or {@link #files}
     * otherwise.
     */
    public static final int unknownOrFiles(boolean recurse)
    {
        return (recurse ? unknown : files);
    }

    /**
     * @return A depth value of {@link #unknown} when
     * <code>recurse</code> is <code>true</code>, or {@link
     * #immediates} otherwise.
     */
    public static final int unknownOrImmediates(boolean recurse)
    {
        return (recurse ? unknown : immediates);
    }
}
