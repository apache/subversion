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

    private long revision = 0;
    private String url = null;
    private int nodekind = NODEKIND_NONE;
    private int schedule = SCHEDULE_NORMAL;
    private int existence = EXISTENCE_NORMAL;
    private Date texttime = null;
    private Date proptime = null;
    private Hashtable attributes = new Hashtable();

    public Entry()
        {
        }

    public void setRevision(long _revision)
        {
            revision = _revision;
        }
    public long getRevision()
        {
            return revision;
        }
    public void setUrl(String _url)
        {
            url = _url;
        }
    public String getUrl()
        {
            return url;
        }
    public void setNodekind(int _nodekind)
        {
            nodekind = _nodekind;
        }
    public int getNodekind()
        {
            return nodekind;
        }
    public void setSchedule(int _schedule)
        {
            schedule = _schedule;
        }
    public int getSchedule()
        {
            return schedule;
        }
    public void setExistence(int _existence)
        {
            existence = _existence;
        }
    public int getExistence()
        {
            return existence;
        }
    public void setTexttime(Date _texttime)
        {
            texttime = _texttime;
        }
    public Date getTexttime()
        {
            return texttime;
        }
    public void setProptime(Date _proptime)
        {
            proptime = _proptime;
        }
    public Date getProptime()
        {
            return proptime;
        }
    public void setAttributes(Hashtable _attributes)
        {
            attributes = _attributes;
        }
    public Hashtable getAttributes()
        {
            return attributes;
        }
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */




