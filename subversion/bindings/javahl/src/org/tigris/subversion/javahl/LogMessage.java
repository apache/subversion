/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004,2007 CollabNet.  All rights reserved.
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
 * This class describes a single subversion revision with log message,
 * author and date.
 */
public class LogMessage implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * The log message for the revision.
     */
    private String message;

    /**
     * The time of the commit measured in the number of microseconds
     * since 00:00:00 January 1, 1970 UTC.
     */
    private long timeMicros;

    /**
     * The date of the commit.
     */
    private Date date;

    /**
     * The number of the revision.
     */
    private long revision;

    /**
     * The author of the commit.
     */
    private String author;

    /**
     * The items changed by this commit (only set when
     * SVNClientInterface.logMessages is used with discoverPaths
     * true).
     */
    private ChangePath[] changedPaths;

    /**
     * This constructor is the original constructor from Subversion
     * 1.4 and older.
     * @param changedPaths the items changed by this commit
     * @param revision     the number of the revision
     * @param author       the author of the commit
     * @param date         the date of the commit
     * @param message      the log message text
     * @deprecated         Use the constructor that takes the number
     *                     of microseconds since 00:00:00 January 1,
     *                     1970 UTC
     */
    LogMessage(ChangePath[] cp, long r, String a, Date d, String m)
    {
        changedPaths = cp;
        revision = r;
        author = a;
        timeMicros = 1000 * d.getTime();
        date = d;
        message = m;
    }

    /**
     * This constructor is only called only from the thin wrapper.
     * @param changedPaths the items changed by this commit
     * @param revision     the number of the revision
     * @param author       the author of the commit
     * @param timeMicros   the time of the commit measured in the
     *                     number of microseconds since 00:00:00
     *                     January 1, 1970 UTC
     * @param message      the log message text
     * @since 1.5
     */
    LogMessage(ChangePath[] cp, long r, String a, long t, String m)
    {
        changedPaths = cp;
        revision = r;
        author = a;
        timeMicros = t;
        date = null;
        message = m;
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
     * Returns the time of the commit
     * @return the time of the commit measured in the number of
     *         microseconds since 00:00:00 January 1, 1970 UTC
     * @since 1.5
     */
    public long getTimeMicros()
    {
        return timeMicros;
    }

    /**
     * Returns the time of the commit
     * @return the time of the commit measured in the number of
     *         milliseconds since 00:00:00 January 1, 1970 UTC
     * @since 1.5
     */
    public long getTimeMillis()
    {
        return timeMicros / 1000;
    }

    /**
     * Returns the date of the commit
     * @return the date of the commit
     */
    public Date getDate()
    {
        if (date == null)
        {
            date = new Date(timeMicros / 1000);
        }
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
     * Returns the revision as a long integer
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
