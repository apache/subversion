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

public class Status 
{
    public final Entry entry;
    public final Revision repos_rev;
    public final StatusKind text_status;
    public final StatusKind prop_status;
    public final boolean locked;
    public final StatusKind repos_text_status;
    public final StatusKind repos_prop_status;

    public Status(Entry entry, Revision repos_rev,
		  StatusKind text_status,
		  StatusKind prop_status,
		  boolean locked,
		  StatusKind repos_text_status,
		  StatusKind repos_prop_status)
	{
	    this.entry = entry;
	    this.repos_rev = repos_rev;
	    this.text_status = text_status;
	    this.prop_status = prop_status;
	    this.locked = locked;
	    this.repos_text_status = repos_text_status;
	    this.repos_prop_status = repos_prop_status;
	}
    public Status(Entry entry, int repos_rev,
		  int text_status, int prop_status,
		  boolean locked, 
		  int repos_text_status,
		  int repos_prop_status)
	{
	    this(entry, new Revision(repos_rev),
		   new StatusKind(text_status),
		   new StatusKind(prop_status),
		   locked,
		   new StatusKind(repos_text_status),
		   new StatusKind(repos_prop_status));
	}
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */



