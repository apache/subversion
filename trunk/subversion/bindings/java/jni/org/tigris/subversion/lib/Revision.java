package org.tigris.subversion.lib;

/**
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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

public final class Revision {
    public final static long INVALID_REVISION=-1;
    private final long revision;

    public Revision(long _revision)
    {
	super();

	revision = _revision;
    }

    public Revision(Revision _revision)
    {
	this(_revision.toLong());
    }

    public static boolean isValidRevision(Revision _revision)
    {
	return isValidRevision(_revision.toLong());
    }

    public static boolean isValidRevision(long _revision)
    {
	return _revision >= 0;
    }

    public final long toLong()
    {
	return revision;
    }

    public static long toLong(Revision _revision)
    {
	return _revision.toLong();
    }
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */

