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
 **/

import java.util.Date;
import java.util.Hashtable;

public class Entry {
    public final static int SCHEDULE_NORMAL=0;
    public final static int SCHEDULE_ADD=1;
    public final static int SCHEDULE_DELETE=2;
    public final static int SCHEDULE_REPLACE=3;
    public final static int SCHEDULE_UNADD=4;
    public final static int SCHEDULE_UNDELETE=5;

    public final static int EXISTENCE_NORMAL=0;
    public final static int EXISTENCE_ADDED=1;
    public final static int EXISTENCE_DELETED=2;

    public final static int NODEKIND_NONE = 0;
    public final static int NODEKIND_FILE = 1;
    public final static int NODEKIND_DIR = 2;
    public final static int NODEKIND_UNKNOWN = 3;

    public final long revision;
    public final String url;
    public final int nodeKind;
    public final int schedule;
    public final int existence;
    public final Date text_time;
    public final Date prop_time;
    public final Hashtable attributes;

    public Entry( long _revision, String _url, int _nodeKind, 
                  int _schedule, int _existence, Date _text_time, 
                  Date _prop_time, Hashtable _attributes )
	{
	    url = _url;
	    revision = _revision;
	    nodeKind = _nodeKind;
	    schedule = _schedule;
	    existence = _existence;
            text_time = _text_time;
            prop_time = _prop_time;
            attributes = _attributes;
	}
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */




