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

public class Status 
{
    private Entry entry = null;
    private StatusKind textStatus = null;
    private StatusKind propStatus = null;
    private boolean copied = false;
    private boolean locked = false;
    private StatusKind reposTextStatus = null;
    private StatusKind reposPropStatus = null;

    public Status()
        {
            super();
        }

    public Status(Status status)
        {
            this();
            
            setEntry(status.getEntry());
            setTextStatus(status.getTextStatus());
            setPropStatus(status.getPropStatus());
            setCopied(status.getCopied());
            setLocked(status.getLocked());
            setReposTextStatus(status.getReposTextStatus());
            setReposPropStatus(status.getReposPropStatus());
        }

    public void setEntry(Entry _entry)
        {
            entry = _entry;
        }
    
    public Entry getEntry()
        {
            return entry;
        }

    public void setTextStatus(StatusKind _textStatus)
        {
            textStatus = _textStatus;
        }

    public StatusKind getTextStatus()
        {
            return textStatus;
        }

    public void setPropStatus(StatusKind _propStatus)
        {
            propStatus = _propStatus;
        }

    public StatusKind getPropStatus()
        {
            return propStatus;
        }

    public void setCopied(boolean _copied)
        {
            copied = _copied;
        }

    public boolean getCopied()
        {
            return copied;
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

    public StatusKind getReposTextStatus()
        {
            return reposTextStatus;
        }

    public void setReposPropStatus(StatusKind _reposPropStatus)
        {
            reposPropStatus = _reposPropStatus;
        }

    public StatusKind getReposPropStatus()
        {
            return reposPropStatus;
        }
}
