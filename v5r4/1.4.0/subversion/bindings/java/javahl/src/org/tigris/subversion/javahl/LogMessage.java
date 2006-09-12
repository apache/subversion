package org.tigris.subversion.javahl;

import java.util.Date;

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
/**
 * this class describes a single subversion revision with log message,
 * author and date
 */
public class LogMessage
{
    /**
     * the log message for the revision
     */
    private String message;
    /**
     * the date of the commit
     */
    private Date date;
    /**
     * the number of the revision
     */
    private long revision;
    /**
     * the author of the commit
     */
    private String author;

    /**
     * the items changed by this commit (only set when
     * SVNClientInterface.logmessage is used with discoverPaths true.
     */
    private ChangePath[] changedPaths;

    /**
     * this constructor is only called only from JNI code
     * @param m     the log message text
     * @param d     the date of the commit
     * @param r     the number of the revision
     * @param a     the author of the commit
     * @param cp    the items changed by this commit
     */
    LogMessage(String m, Date d, long r, String a, ChangePath[] cp)
    {
        message = m;
        date = d;
        revision = r;
        author = a;
        changedPaths = cp;
    }
    /**
     * Return the log message text
     * @return the log message text
     */
    public String getMessage()
    {
        return message;
    }

    /**
     * Returns the date of the commit
     * @return the date of the commit
     */
    public Date getDate()
    {
        return date;
    }

    /**
     * Returns the revision as a Revision object
     * @return the revision number as a Revision object
     */
    public Revision.Number getRevision()
    {
        return Revision.createNumber(revision);
    }

    /**
     * Returns the revision as a long  integer
     * @return the revision number as a long integer
     */
    public long getRevisionNumber()
    {
        return revision;
    }

    /**
     * Returns the author of the commit
     * @return the author of the commit
     */
    public String getAuthor()
    {
        return author;
    }

    /**
     * Returns the changes items by this commit
     * @return the changes items by this commit
     */
    public ChangePath[] getChangedPaths()
    {
        return changedPaths;
    }

}
