package org.tigris.subversion.javahl;

import java.util.Date;

/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
public class LogMessage
{
    private String message;
    private Date date;
    private long revision;
    private String author;

    LogMessage(String m, Date d, long r, String a)
    {
        message = m;
        date = d;
        revision = r;
        author = a;
    }
    public String getMessage()
    {
        return message;
    }

    public Date getDate()
    {
        return date;
    }

    public Revision.Number getRevision()
    {
        return new Revision.Number(revision);
    }

    public long getRevisionNumber()
    {
        return revision;
    }

    public String getAuthor()
    {
        return author;
    }
}
