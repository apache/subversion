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
 * The description of a merge conflict, encountered during
 * merge/update/switch operations.
 *
 * @since 1.5
 */
public class ConflictDescriptor
{
    private String path;

    /**
     * @see NodeKind
     */
    private int nodeKind;

    private boolean isBinary;
    private String mimeType;

    // svn_wc_conflict_description_t also provides us with an
    // svn_wc_adm_access_t *.  However, that is only useful to
    // JNI-based APIs written against svn_wc.h.  So, we don't (yet)
    // expose that to JavaHL.  We could expose it is a long
    // representing the memory address of the struct, which could be
    // passed off to other JNI APIs.

    /**
     * @see #Action
     */
    private int action;

    /**
     * @see #Reason
     */
    private int reason;

    // File paths, present only when the conflict involves the merging
    // of two files descended from a common ancestor, here are the
    // paths of up to four fulltext files that can be used to
    // interactively resolve the conflict.  NOTE: The content of these
    // files will be in repository-normal form (LF line endings and
    // contracted keywords).
    private String basePath;
    private String reposPath;
    private String userPath;
    private String mergedPath;

    ConflictDescriptor(String path, int nodeKind, boolean isBinary,
                       String mimeType, int action, int reason,
                       String basePath, String reposPath,
                       String userPath, String mergedPath)
    {
        this.path = path;
        this.nodeKind = nodeKind;
        this.isBinary = isBinary;
        this.mimeType = mimeType;
        this.action = action;
        this.reason = reason;
        this.basePath = basePath;
        this.reposPath = reposPath;
        this.userPath = userPath;
        this.mergedPath = mergedPath;
    }

    public String getPath()
    {
        return path;
    }

    /**
     * @see NodeKind
     */
    public int getNodeKind()
    {
        return nodeKind;
    }

    public boolean isBinary()
    {
        return isBinary;
    }

    public String getMIMEType()
    {
        return mimeType;
    }

    /**
     * @see .Action
     */
    public int getAction()
    {
        return action;
    }

    /**
     * @see .Reason
     */
    public int getReason()
    {
        return reason;
    }

    public String getBasePath()
    {
        return basePath;
    }

    public String getReposPath()
    {
        return reposPath;
    }

    public String getUserPath()
    {
        return userPath;
    }

    public String getMergedPath()
    {
        return mergedPath;
    }

    /**
     * Poor man's enum for <code>svn_wc_conflict_action_t</code>.
     */
    public final class Action
    {
        /**
         * Attempting to change text or props.
         */
        public static final int edit = 0;

        /**
         * Attempting to add object.
         */
        public static final int add = 1;

        /**
         * Attempting to delete object.
         */
        public static final int delete = 2;
    }

    /**
     * Poor man's enum for <code>svn_wc_conflict_reason_t</code>.
     */
    public final class Reason
    {
        /**
         * Local edits are already present.
         */
        public static final int edited = 0;

        /**
         * Another object is in the way.
         */
        public static final int obstructed = 1;

        /**
         * Object is already schedule-delete.
         */
        public static final int deleted = 2;

        /**
         * Object is unknown or missing.
         */
        public static final int missing = 3;

        /**
         * Object is unversioned.
         */
        public static final int unversioned = 4;
    }
}
