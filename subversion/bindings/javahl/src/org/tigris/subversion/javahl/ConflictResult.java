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
 * The result returned by the callback API used to handle conflicts
 * encountered during merge/update/switch operations.  Includes a poor
 * man's enum for <code>svn_wc_conflict_choice_t</code>.
 *
 * @since 1.5
 */
public class ConflictResult
{
    /**
     * Nothing done to resolve the conflict; conflict remains.
     */
    public static final int postpone = 0;

    /**
     * Resolve the conflict by choosing the base file.
     */
    public static final int chooseBase = 1;

    /**
     * Resolve the conflict by choosing the incoming (repository)
     * version of the object.
     */
    public static final int chooseTheirsFull = 2;

    /**
     * Resolve the conflict by choosing own (local) version of the
     * object.
     */
    public static final int chooseMineFull = 3;

    /**
     * Resolve the conflict by choosing the incoming (repository)
     * version of the object (for conflicted hunks only).
     */
    public static final int chooseTheirsConflict = 4;

    /**
     * Resolve the conflict by choosing own (local) version of the
     * object (for conflicted hunks only).
     */
    public static final int chooseMineConflict = 5;

    /**
     * Resolve the conflict by choosing the merged object
     * (potentially manually edited).
     */
    public static final int chooseMerged = 6;

    /**
     * A value corresponding to the
     * <code>svn_wc_conflict_choice_t</code> enum.
     */
    private int choice;

    /**
     * The path to the result of a merge, or <code>null</code>.
     */
    private String mergedPath;

    /**
     * Create a new conflict result instace.
     */
    public ConflictResult(int choice, String mergedPath)
    {
      this.choice = choice;
      this.mergedPath = mergedPath;
    }

    /**
     * @return A value corresponding to the
     * <code>svn_wc_conflict_choice_t</code> enum.
     */
    public int getChoice()
    {
        return choice;
    }

    /**
     * @return The path to the result of a merge, or <code>null</code>.
     */
    public String getMergedPath()
    {
        return mergedPath;
    }
}
