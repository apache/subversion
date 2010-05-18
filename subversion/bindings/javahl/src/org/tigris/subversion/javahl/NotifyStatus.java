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
 * Status of the text or the property of the item triggering the
 * notification.
 */
public interface NotifyStatus
{
    /** It not applicable*/
    public static final int inapplicable = 0;

    /** Notifier doesn't know or isn't saying. */
    public static final int unknown = 1;

    /** The state did not change. */
    public static final int unchanged = 2;

    /** The item wasn't present. */
    public static final int missing = 3;

    /** An unversioned item obstructed work. */
    public static final int obstructed = 4;

    /** Pristine state was modified. */
    public static final int changed = 5;

    /** Modified state had mods merged in. */
    public static final int merged = 6;

    /** Modified state got conflicting mods. */
    public static final int conflicted = 7;

    /**
     * the textual represention for the status types
     */
    public static final String[] statusNames =
    {
        "inapplicable",
        "unknown",
        "unchanged",
        "missing",
        "obstructed",
        "changed",
        "merged",
        "conflicted",
    };
}
