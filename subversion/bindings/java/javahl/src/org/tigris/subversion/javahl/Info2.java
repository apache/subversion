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
 */
public class Info2
{
    /**
     * the path of the item
     */
    private String path;
    /**
     * the url of the item
     */
    private String url;
    /**
     * the revision of the item
     */
    private long rev;
    /**
     * item kinds see NodeKind
     */
    private int kind;
    /**
     * the root URL of the repository
     */
    private String reposRootUrl;
    /**
     * the UUID of the repository
     */
    private String reposUUID;
    /**
     * the revision of the last change
     */
    private long lastChangedRev;
    /**
     * the date of the last change in ns
     */
    private long lastChangedDate;
    /**
     * the author of the last change
     */
    private String lastChangedAuthor;
    /**
     * the information about any lock (may be null)
     */
    private Lock lock;
    /**
     * flag if the remaining field are set
     */
    private boolean hasWcInfo;
    /**
     * the scheduled operation at next commit (see ScheduleKind)
     */
    private int schedule;
    /**
     * if the item was copied, the source url
     */
    private String copyFromUrl;
    /**
     * if the item was copied, the source rev
     */
    private long copyFromRev;
    /**
     * the last time the item was changed in ns
     */
    private long textTime;
    /**
     * the last time the properties of the items were changed in ns
     */
    private long propTime;
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
          String reposUUID, long lastChangedRev, long lastChangedDate,
          String lastChangedAuthor, Lock lock, boolean hasWcInfo, int schedule,
          String copyFromUrl, long copyFromRev, long textTime, long propTime,
          String checksum, String conflictOld, String conflictNew,
          String conflictWrk, String prejfile)
    {
        this.path = path;
        this.url = url;
        this.rev = rev;
        this.kind = kind;
        this.reposRootUrl = reposRootUrl;
        this.reposUUID = reposUUID;
        this.lastChangedRev = lastChangedRev;
        this.lastChangedDate = lastChangedDate;
        this.lastChangedAuthor = lastChangedAuthor;
        this.lock = lock;
        this.hasWcInfo = hasWcInfo;
        this.schedule = schedule;
        this.copyFromUrl = copyFromUrl;
        this.copyFromRev = copyFromRev;
        this.textTime = textTime;
        this.propTime = propTime;
        this.checksum = checksum;
        this.conflictOld = conflictOld;
        this.conflictNew = conflictNew;
        this.conflictWrk = conflictWrk;
        this.prejfile = prejfile;
    }

    public String getPath()
    {
        return path;
    }

    public String getUrl()
    {
        return url;
    }

    public long getRev()
    {
        return rev;
    }

    public int getKind()
    {
        return kind;
    }

    public String getReposRootUrl()
    {
        return reposRootUrl;
    }

    public String getReposUUID()
    {
        return reposUUID;
    }

    public long getLastChangedRev()
    {
        return lastChangedRev;
    }

    public Date getLastChangedDate()
    {
        if(lastChangedDate == 0)
            return null;
        else
            return new Date(lastChangedDate/1000);
    }

    public String getLastChangedAuthor()
    {
        return lastChangedAuthor;
    }

    public Lock getLock()
    {
        return lock;
    }

    public boolean isHasWcInfo()
    {
        return hasWcInfo;
    }

    public int getSchedule()
    {
        return schedule;
    }

    public String getCopyFromUrl()
    {
        return copyFromUrl;
    }

    public long getCopyFromRev()
    {
        return copyFromRev;
    }

    public Date getTextTime()
    {
        if(textTime == 0)
            return null;
        else
            return new Date(textTime/1000);
    }

    public Date getPropTime()
    {
        if(propTime == 0)
            return null;
        else
            return new Date(propTime/1000);
    }

    public String getChecksum()
    {
        return checksum;
    }

    public String getConflictOld()
    {
        return conflictOld;
    }

    public String getConflictNew()
    {
        return conflictNew;
    }

    public String getConflictWrk()
    {
        return conflictWrk;
    }

    public String getPrejfile()
    {
        return prejfile;
    }
}
