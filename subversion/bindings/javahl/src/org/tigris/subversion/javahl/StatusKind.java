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
 * class for kind status of the item or its properties
 */
public interface StatusKind
{
    /** does not exist */
    public static final int none = 0;

    /** exists, but uninteresting */
    public static final int normal = 1;

    /** text or props have been modified */
    public static final int modified = 2;

    /** is scheduled for additon */
    public static final int added = 3;

    /** scheduled for deletion */
    public static final int deleted = 4;

    /** is not a versioned thing in this wc */
    public static final int unversioned = 5;

    /** under v.c., but is missing */
    public static final int missing = 6;

    /** was deleted and then re-added */
    public static final int replaced = 7;

    /** local mods received repos mods */
    public static final int merged = 8;

    /** local mods received conflicting repos mods */
    public static final int conflicted = 9;

    /** an unversioned resource is in the way of the versioned resource */
    public static final int obstructed = 10;

    /** a resource marked as ignored */
    public static final int ignored = 11;

    /** a directory doesn't contain a complete entries list */
    public static final int incomplete = 12;

    /** an unversioned path populated by an svn:externals property */
    public static final int external = 13;
}
