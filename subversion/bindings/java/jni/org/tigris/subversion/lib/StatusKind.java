package org.tigris.subversion.lib;

/**
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
 *
 */

public final class StatusKind 
{
    /**
     * IMPORTANT: REMEMBER TO KEEP THIS
     * CONSTANTS IN SYNC WITH THE ENUM
     * svn_wc_status_kind_t
     */
    public final static int NONE=1;
    public final static int UNVERSIONED=2;
    public final static int NORMAL=3;
    public final static int ADDED=4;
    public final static int ABSENT=5;
    public final static int DELETED=6;
    public final static int REPLACED=7;
    public final static int MODIFIED=8;
    public final static int MERGED=9;
    public final static int CONFLICTED=10;

    private final int kind;

    public StatusKind(int _kind) 
	{
	    super();
	    kind = _kind;
	}

    public StatusKind(StatusKind statusKind)
	{
	    this(statusKind.getKind());
	}

    public int getKind()
	{
	    return kind;
	}
}







