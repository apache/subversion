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

public final class Nodekind 
{
    /**
     * IMPORTANT: KEEP THIS IN SYNC WITH THE
     * DEFINITION OF svn_node_kind_t
     */
    public final static int NONE=0;
    public final static int FILE=1;
    public final static int DIR=2;
    public final static int UNKNOWN=3;

    private final int kind;

    public Nodekind(int _kind) 
	{
	    super();
	    kind = _kind;
	}

    public Nodekind(Nodekind nodekind)
	{
	    this(nodekind.getKind());
	}

    public final int getKind()
	{
	    return kind;
	}
}
