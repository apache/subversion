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
 * ====================================================================
 *
 */

public class Revision {
    public final static int INVALID_REVISION=-1;
    private int revision=INVALID_REVISION;

    public Revision(int revision)
    {
	super();

	this.revision = revision;
    }

    public Revision(Revision revision)
    {
	this(revision.toInteger());
    }

    public static boolean isValidRevision(Revision revision)
    {
	return isValidRevision(revision.toInteger());
    }

    public static boolean isValidRevision(int revision)
    {
	return revision >= 0;
    }

    public int toInteger()
    {
	return revision;
    }

    public static int toInteger(Revision revision)
    {
	return revision.toInteger();
    }
}