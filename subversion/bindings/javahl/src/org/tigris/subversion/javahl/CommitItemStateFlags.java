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
 * The constants in this interface describe the changes to an item to
 * be commited.
 */
public interface CommitItemStateFlags
{
    /**
     * the item has been added
     */
    public static final int Add=1;

    /**
     * the item has been deleted
     */
    public static final int Delete=2;

    /**
     * the item has text modifications
     */
    public static final int TextMods=4;

    /**
     * the item has property modifications
     */
    public static final int PropMods=8;

    /**
     * the item has been copied
     */
    public static final int IsCopy=16;
}
