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
 **/

import java.util.Date;
import java.util.Hashtable;

public class Entry {
    
    private Revision revision = null;
    private String url = null;
    private Nodekind kind = null;
    private Schedule schedule = null;
    private boolean conflicted = false;
    private boolean copied = false;
    private Date texttime = null;
    private Date proptime = null;
    private Hashtable attributes = new Hashtable();

    public Entry()
        {
            super();
        }

    public void setRevision(Revision _revision)
        {
            revision = _revision;
        }
    public Revision getRevision()
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
    public void setKind(Nodekind _kind)
        {
            kind = _kind;
        }
    public Nodekind getKind()
        {
            return kind;
        }
    public void setSchedule(Schedule _schedule)
        {
            schedule = _schedule;
        }
    public Schedule getSchedule()
        {
            return schedule;
        }
    public void setConflicted(boolean _conflicted)
        {
            conflicted = _conflicted;
        }
    public boolean getConflicted()
        {
            return conflicted;
        }
    public void setCopied(boolean _copied)
        {
            copied = _copied;
        }
    public boolean getCopied()
        {
            return copied;
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




