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
    private Entry entry = null;
    private Revision reposRev = null;
    private StatusKind textStatus = null;
    private StatusKind propStatus = null;
    private boolean locked = false;
    private StatusKind reposTextStatus = null;
    private StatusKind reposPropStatus = null;

    public Status()
        {
        }

    public void setEntry(Entry _entry)
        {
            entry = _entry;
        }
    
    public Entry getEntry()
        {
            return entry;
        }

    public void setReposRev(Revision _reposRev)
        {
            reposRev = _reposRev;
        }

    public void setReposRev(long _reposRev)
        {
            reposRev = new Revision(_reposRev);
        }

    public Revision getReposRev()
        {
            return reposRev;
        }

    public void setTextStatus(StatusKind _textStatus)
        {
            textStatus = _textStatus;
        }

    public void setTextStatus(int _textStatus)
        {
            textStatus = new StatusKind(_textStatus);
        }

    public StatusKind getTextStatus()
        {
            return textStatus;
        }

    public void setPropStatus(StatusKind _propStatus)
        {
            propStatus = _propStatus;
        }

    public void setPropStatus(int _propStatus)
        {
            propStatus = new StatusKind(_propStatus);
        }

    public StatusKind getPropStatus()
        {
            return propStatus;
        }

    public void setLocked(boolean _locked)
        {
            locked = _locked;
        }

    public boolean getLocked()
        {
            return locked;
        }

    public void setReposTextStatus(StatusKind _reposTextStatus)
        {
            reposTextStatus = _reposTextStatus;
        }

    public void setReposTextStatus(int _reposTextStatus)
        {
            reposTextStatus = new StatusKind(_reposTextStatus);
        }

    public StatusKind getReposTextStatus()
        {
            return reposTextStatus;
        }

    public void setReposPropStatus(StatusKind _reposPropStatus)
        {
            reposPropStatus = _reposPropStatus;
        }

    public void setReposPropStatus(int _reposPropStatus)
        {
            reposPropStatus = new StatusKind(_reposPropStatus);
        }

    public StatusKind getReposPropStatus()
        {
            return reposPropStatus;
        }
}

/* 
 * local variables:
 * eval: (load-file "../../../../../../../svn-dev.el")
 * end: 
 */



