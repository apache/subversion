/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
 * @since 1.2
 * what happened to a lock during an operation
 */
public interface LockStatus
{
    /**
     * does not make sense for this operation
     */
    public static final int inapplicable = 0;

    /**
     * unknown lock state
     */
    public static final int unknown = 1;

    /**
     * the lock change did not change
     */
    public static final int unchanged = 2;

    /**
     * the item was locked
     */
    public static final int locked = 3;

    /**
     * the item was unlocked
     */
    public static final int unlocked = 4;

    public static final String[] stateNames =
    {
        "inapplicable",
        "unknown",
        "unchanged",
        "locked",
        "unlocked",
    };
}
