/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003 CollabNet.  All rights reserved.
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
 * @endcopyright
 */
package org.tigris.subversion.javahl;
import java.util.Date;


/**
 * Subversion status API.
 * @author Patrick Mayweg
 * @author Cédric Chabanois 
 *         <a href="mailto:cchabanois@ifrance.com">cchabanois@ifrance.com</a> 
 */
public class Status
{
    private String url;              // url in repository    
    private String path;
    private int nodeKind;                // node kind (file, dir, ...)
    private long revision;           // base revision
    private long lastChangedRevision;// last revision this was changed
    private long lastChangedDate;    // last date this was changed 
    private String lastCommitAuthor; // last commit author of this item
    private int textStatus;
    private int propStatus;
    private boolean locked;
    private boolean copied;          // in a copied state 
    private int repositoryTextStatus;
    private int repositoryPropStatus;
    private String conflictNew;      // new version of conflicted file 
    private String conflictOld;      // old version of conflicted file
    private String conflictWorking;  // working version of conflicted file
    private String urlCopiedFrom;
    private long revisionCopiedFrom;

    
    public Status(String path, String url, int nodeKind, long revision, 
        long lastChangedRevision, long lastChangedDate, String lastCommitAuthor, 
        int textStatus, int propStatus, 
        int repositoryTextStatus, int repositoryPropStatus,
        boolean locked, boolean copied, 
        String conflictOld, String conflictNew, String conflictWorking,
        String urlCopiedFrom, long revisionCopiedFrom)
    {
        this.path = path;
        this.url = url;
        this.nodeKind = nodeKind;
        this.revision = revision;
        this.lastChangedRevision = lastChangedRevision;
        this.lastChangedDate = lastChangedDate;
        this.lastCommitAuthor = lastCommitAuthor;
        this.textStatus = textStatus;
        this.propStatus = propStatus;
        this.locked = locked;
        this.copied = copied;
        this.repositoryTextStatus = repositoryTextStatus;
        this.repositoryPropStatus = repositoryPropStatus;
        this.conflictOld = conflictOld;
        this.conflictNew = conflictNew;
        this.conflictWorking = conflictWorking;
        this.urlCopiedFrom = urlCopiedFrom;
        this.revisionCopiedFrom = revisionCopiedFrom;
    }

    /**
     * @return path of status entry
     */
    public String getPath()
    {
        return path;
    }

    /**
     * @return revision if versioned, otherwise SVN_INVALID_REVNUM
     */
    public Revision.Number getRevision()
    {
        return new Revision.Number(revision);
    }

    /**
     * @return revision if versioned, otherwise SVN_INVALID_REVNUM
     */
    public long getRevisionNumber()
    {
        return revision;
    }

    /**
     * @return the last time the file was changed revision number.
     * or null if not available
     */
    public Date getLastChangedDate()
    {
        if (lastChangedDate == 0)
            return null;
        else
            return new Date(lastChangedDate/1000);
    }
    /**
     * @return name of author if versioned, NULL otherwise
     */
    public String getLastCommitAuthor()
    {
        return lastCommitAuthor;
    }

    /**
     * @return file status property enum of the "textual" component.
     */
    public int getTextStatus()
    {
        return textStatus;
    }

    public String getTextStatusDescription()
    {
        return Kind.getDescription(textStatus);
    }
    /**
     * @return file status property enum of the "property" component.
     */
    public int getPropStatus()
    {
        return propStatus;
    }

    public String getPropStatusDescription()
    {
        return Kind.getDescription(propStatus);
    }

    /**
     * @return file status property enum of the "textual" component im the repository.
     */
    public int getRepositoryTextStatus()
    {
        return repositoryTextStatus;
    }

    /**
     * @return file status property enum of the "property" component im the repository.
     */
    public int getRepositoryPropStatus()
    {
        return repositoryPropStatus;
    }

    /**
     * @return true if locked
     */
     public boolean isLocked()
    {
        return locked;
    }
    /**
     * @return true if copied
     */
    public boolean isCopied()
    {
        return copied;
    }
    public String getConflictNew()
    {
        return conflictNew;
    }
    public String getConflictOld()
    {
        return conflictOld;
    }
    public String getConflictWorking()
    {
        return conflictWorking;
    }

    /**
     * @return url in repository or null if not known
     */
    public String getUrl() {
        return url;
    }


    /**
     * @return last changed revision
     */
    public Revision.Number getLastChangedRevision()
    {
        return new Revision.Number(lastChangedRevision);
    }

    /**
     * @return last changed revision
     */
    public long getLastChangedRevisionNumber()
    {
        return lastChangedRevision;
    }

    /**
     * @return the node kind
     */
    public int getNodeKind()
    {
        return nodeKind;
    }


    public String getUrlCopiedFrom()
    {
        return urlCopiedFrom;
    }
    
    public Revision.Number getRevisionCopiedFrom()
    {
        return new Revision.Number(revisionCopiedFrom);
    }

    public long getRevisionCopiedFromNumber()
    {
        return revisionCopiedFrom;
    }
    /**
     * tells if is managed by svn (added, normal, modified ...)
     * @return
     */
    public boolean isManaged()
    {
        int textStatus = getTextStatus();
        return ((textStatus != Status.Kind.unversioned) && 
                (textStatus != Status.Kind.none) &&
                (textStatus != Status.Kind.ignored));
    }
    
    /**
     * tells if the resource has a remote counter-part
     * @return
     */
    public boolean hasRemote()
    {
        int textStatus = getTextStatus();
        return ((isManaged()) && (textStatus != Status.Kind.added));
    }
    
    public boolean isAdded()
    {
        int textStatus = getTextStatus();
        return textStatus == Status.Kind.added;
    }

    public boolean isDeleted()
    {
        int textStatus = getTextStatus();
        return textStatus == Status.Kind.deleted;        
    }

    public boolean isMerged()
    {
        int textStatus = getTextStatus();
        return textStatus == Status.Kind.merged;        
    }

    public boolean isIgnored()
    {
        int textStatus = getTextStatus();
        return textStatus == Status.Kind.ignored;        
    }

    /**
     * tells if it is modified
     * @return
     */
    public boolean isModified()
    {
        int textStatus = getTextStatus();
        return textStatus == Status.Kind.modified;
    }

    public static final class Kind
    {
        /** does not exist */
        public static final int none = 0;

        /** exists, but uninteresting. */
        public static final int normal = 1;

		/** text or props have been modified */
		public static final int modified = 2;

        /** is scheduled for additon */
        public static final int added = 3;

        /** scheduled for deletion */
        public static final int deleted = 4;

		/** is not a versioned thing in this wc */
		public static final int unversioned = 5;

		/** under v.c., but is missing */
		public static final int absent = 6;

        /** was deleted and then re-added */
        public static final int replaced = 7;

        /** local mods received repos mods */
        public static final int merged = 8;

        /** local mods received conflicting repos mods */
        public static final int conflicted = 9;

        /** an unversioned resource is in the way of the versioned resource */
        public static final int obstructed = 10;

        /** a resource marked as ignored */
        public static final int ignored = 11;
        
        /** a directory doesn't contain a complete entries list  */
        public static final int incomplete = 12;

        public static final String getDescription(int kind)
        {
            switch (kind)
            {
            case none:
              return "non-svn";
            case normal:
              return "normal";
            case added:
              return "added";
            case absent:
              return "absent";
            case deleted:
              return "deleted";
            case replaced:
              return "replaced";
            case modified:
              return "modified";
            case merged:
              return "merged";
            case conflicted:
              return "conflicted";
            case ignored:
              return "ignored";
            case incomplete:
              return "incomplete";
            case unversioned:
            default:
              return "unversioned";
            }
        }
    }


}

