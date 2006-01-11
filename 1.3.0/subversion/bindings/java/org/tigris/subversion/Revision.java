package org.tigris.subversion;

/*
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
 */

/**
 * The methods of this interface correspond to the types and functions
 * described in the subversion C api located in 'svn_client.h'.
 */
public class Revision extends Number
{
    private long revision;

    public Revision(long revision)
    {
        this.revision = revision;
    }

    public Revision(Number revision)
    {
        this(revision.longValue());
    }

    public Revision(String revision)
    {
        this(Long.parseLong(revision));
    }

    public double doubleValue()
    {
        return (double) revision;
    }

    public float floatValue()
    {
        return (float) revision;
    }

    public int intValue()
    {
        return (int) revision;
    }

    public long longValue()
    {
        return revision;
    }
}
