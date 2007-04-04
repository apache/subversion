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

import java.util.Date;

/**
 * this class is returned by SVNClientInterface.info2 and contains information
 * about items in the repository or working copy
 * @since 1.2
 */
public class Info2
    extends Info
{
    /**
     * the information about any lock (may be null)
     */
    private Lock lock;
    /**
     * the flag if the remaining fields are set
     */
    private boolean hasWcInfo;
    /**
     * the checksum of the item
     */
    private String checksum;
    /**
     * if the item is in conflict, the filename of the base version file
     */
    private String conflictOld;
    /**
     * if the item is in conflict, the filename of the last repository version
     * file
     */
    private String conflictNew;
    /**
     * if the item is in conflict, the filename of the working copy version file
     */
    private String conflictWrk;
    /**
     * the property reject file
     */
    private String prejfile;

    /**
     * constructor to build the object by native code. See fields for parameters
     * @param path
     * @param url
     * @param rev
     * @param kind
     * @param reposRootUrl
     * @param reposUUID
     * @param lastChangedRev
     * @param lastChangedDate
     * @param lastChangedAuthor
     * @param lock
     * @param hasWcInfo
     * @param schedule
     * @param copyFromUrl
     * @param copyFromRev
     * @param textTime
     * @param propTime
     * @param checksum
     * @param conflictOld
     * @param conflictNew
     * @param conflictWrk
     * @param prejfile
     */
    Info2(String path, String url, long rev, int kind, String reposRootUrl,
          String reposUUID, long lastChangedRev, Date lastChangedDate,
          String lastChangedAuthor, Lock lock, boolean hasWcInfo, int schedule,
          String copyFromUrl, long copyFromRev, Date textTime, Date propTime,
          String checksum, String conflictOld, String conflictNew,
          String conflictWrk, String prejfile, boolean copied, boolean deleted,
          boolean absent, boolean incomplete)
    {
        super(path, url, reposUUID, reposRootUrl, schedule, kind,
              lastChangedAuthor, rev, lastChangedRev, lastChangedDate,
              textTime, propTime, copied, deleted, absent, incomplete,
              copyFromRev, copyFromUrl);
        this.lock = lock;
        this.hasWcInfo = hasWcInfo;
        this.checksum = checksum;
        this.conflictOld = conflictOld;
        this.conflictNew = conflictNew;
        this.conflictWrk = conflictWrk;
        this.prejfile = prejfile;
    }

    /**
     * return the path of the item
     */
    public String getPath()
    {
        return getName();
    }

    /**
     * return the revision of the item
     */
    public long getRev()
    {
        return getRevision();
    }

    /**
     * return the item kinds (see NodeKind)
     */
    public int getKind()
    {
        return getNodeKind();
    }

    /**
     * return the root URL of the repository
     */
    public String getReposRootUrl()
    {
        return getRepository();
    }

    /**
     * return the UUID of the repository
     */
    public String getReposUUID()
    {
        return getUuid();
    }

    /**
     * return the revision of the last change
     */
    public long getLastChangedRev()
    {
        return getLastChangedRevision();
    }

    /**
     * return the author of the last change
     */
    public String getLastChangedAuthor()
    {
        return getAuthor();
    }

    /**
     * return the information about any lock (may be null)
     */
    public Lock getLock()
    {
        return lock;
    }

    /**
     * return the flag if the working copy fields are set
     */
    public boolean isHasWcInfo()
    {
        return hasWcInfo;
    }

    /**
     * return if the item was copied, the source url
     */
    public String getCopyFromUrl()
    {
        return getCopyUrl();
    }

    /**
     * return if the item was copied, the source rev
     */
    public long getCopyFromRev()
    {
        return getCopyRev();
    }

    /**
     * return the last time the item was changed
     */
    public Date getTextTime()
    {
        return getLastDateTextUpdate();
    }

    /**
     * return the last time the properties of the items were changed
     */
    public Date getPropTime()
    {
        return getLastDatePropsUpdate();
    }

    /**
     * return the checksum of the item
     */
    public String getChecksum()
    {
        return checksum;
    }

    /**
     * return if the item is in conflict, the filename of the base version file
     */
    public String getConflictOld()
    {
        return conflictOld;
    }

    /**
     * return if the item is in conflict, the filename of the last repository
     * version file
     */
    public String getConflictNew()
    {
        return conflictNew;
    }

    /**
     * return if the item is in conflict, the filename of the working copy
     * version file
     */
    public String getConflictWrk()
    {
        return conflictWrk;
    }

    /**
     * return the property reject file
     */
    public String getPrejfile()
    {
        return prejfile;
    }
}
