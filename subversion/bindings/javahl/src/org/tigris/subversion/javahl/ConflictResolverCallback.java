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

import java.util.EventListener;

/**
 * The callback API used to handle conflicts encountered during
 * merge/update/switch operations.
 *
 * @since 1.5
 */
public interface ConflictResolverCallback extends EventListener
{
    /**
     * The callback method invoked for each conflict during a
     * merge/update/switch operation.  NOTE: The files that are
     * potentially passed in the ConflictDescriptor are in
     * repository-normal format (LF line endings and contracted
     * keywords).
     *
     * @param descrip A description of the conflict.
     * @return The result of any conflict resolution, from the {@link
     * .Result} enum.
     * @throws SubversionException If an error occurs.
     * @see .Result
     */
    public int resolve(ConflictDescriptor descrip)
        throws SubversionException;

    /**
     * Poor man's enum for <code>svn_wc_conflict_result_t</code>.
     */
    public final class Result
    {
        /**
         * User did nothing; conflict remains.
         */
        public static final int conflicted = 0;

        /**
         * User has resolved the conflict.
         */
        public static final int resolved = 1;

        /**
         * User chooses the base file.
         */
        public static final int chooseBase = 2;

        /**
         * User chooses the repository file.
         */
        public static final int chooseTheirs = 3;

        /**
         * User chooses own version of file.
         */
        public static final int chooseMine = 4;

        /**
         * User chooses the merged-file (which she may have manually
         * edited).
         */
        public static final int chooseMerged = 5;
    }
}
