package org.tigris.subversion.lib;

/**
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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
 *
 */

public class Revision {
    public final static long INVALID_REVISION=-1;
    private long revision=INVALID_REVISION;

    public Revision(long revision)
    {
	super();

	this.revision = revision;
    }

    public Revision(Revision revision)
    {
	this(revision.toLong());
    }

    public static boolean isValidRevision(Revision revision)
    {
	return isValidRevision(revision.toLong());
    }

    public static boolean isValidRevision(long revision)
    {
	return revision >= 0;
    }

    public long toLong()
    {
	return revision;
    }

    public static long toLong(Revision revision)
    {
	return revision.toLong();
    }
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */

