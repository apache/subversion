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

public interface Entry {
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

    public void setRevision(Revision revision);
    public Revision getRevision();
    public void setAncestor(String ancestor);
    public String getAncestor();
    public void setNodeKind(int nodeKind);
    public int getNodeKind();
}
