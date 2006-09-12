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
 * Various ways of specifying revisions.
 *
 * Note:
 * In contexts where local mods are relevant, the `working' kind
 * refers to the uncommitted "working" revision, which may be modified
 * with respect to its base revision.  In other contexts, `working'
 * should behave the same as `committed' or `current'.
 *
 */
public interface RevisionKind
{
    /** No revision information given. */
    public static final int unspecified = 0;

    /** revision given as number */
    public static final int number = 1;

    /** revision given as date */
    public static final int date = 2;

    /** rev of most recent change */
    public static final int committed = 3;

    /** (rev of most recent change) - 1 */
    public static final int previous = 4;

    /** .svn/entries current revision */
    public static final int base = 5;

    /** current, plus local mods */
    public static final int working = 6;

    /** repository youngest */
    public static final int head = 7;

}
