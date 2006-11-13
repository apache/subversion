/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2004 CollabNet.  All rights reserved.
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
 * Give information about one subversion item (file or directory) in the
 * working copy
 */
public class Info
{
    /** the name of the item */
    private String name;

    /** the url of the item */
    private String url;

    /** the uuid of the repository */
    private String uuid;

    /** the repository url */
    private String repository;

    /** the schedule on the next commit (see NodeKind) */
    private int schedule;

    /** the kind of node (file or directory or unknown */
    private int nodeKind;

    /** the author of the last commit before base */
    private String author;

    /** the last revision this item was updated */
    private long revision;

    /** the last revision the item before base */
    private long lastChangedRevision;

    /** the date of the last commit */
    private Date lastChangedDate;

    /** the last up-to-date time for the text context */
    private Date lastDateTextUpdate;

    /** the last up-to-date time for the properties */
    private Date lastDatePropsUpdate;

    /** the item was copied */
    private boolean copied;

    /** the item was deleted */
    private boolean deleted;

    /** the item is absent */
    private boolean absent;

    /** the item is incomplete */
    private boolean incomplete;

    /** the copy source revision */
    private long copyRev;

    /** the copy source url */
    private String copyUrl;

    /**
     * Constructor to be called only by the native code
     * @param name                  name of the item
     * @param url                   url of the item
     * @param uuid                  uuid of the repository
     * @param repository            url of the repository
     * @param author                author of the last change
     * @param revision              revision of the last update
     * @param lastChangedRevision   revision of the last change
     * @param lastChangedDate       the date of the last change
     * @param lastDateTextUpdate    the date of the last text change
     * @param lastDatePropsUpdate   the date of the last property change
     * @param copied                is the item copied
     * @param deleted               is the item deleted
     * @param absent                is the item absent
     * @param incomplete            is the item incomplete
     * @param copyRev               copy source revision
     * @param copyUrl               copy source url
     */
    Info(String name, String url, String uuid, String repository, int schedule,
         int nodeKind, String author, long revision, long lastChangedRevision,
         Date lastChangedDate, Date lastDateTextUpdate,
         Date lastDatePropsUpdate, boolean copied,
         boolean deleted, boolean absent, boolean incomplete, long copyRev,
         String copyUrl)
    {
        this.name = name;
        this.url = url;
        this.uuid = uuid;
        this.repository = repository;
        this.schedule = schedule;
        this.nodeKind = nodeKind;
        this.author = author;
        this.revision = revision;
        this.lastChangedRevision = lastChangedRevision;
        this.lastChangedDate = lastChangedDate;
        this.lastDateTextUpdate = lastDateTextUpdate;
        this.lastDatePropsUpdate = lastDatePropsUpdate;
        this.copied = copied;
        this.deleted = deleted;
        this.absent = absent;
        this.incomplete = incomplete;
        this.copyRev = copyRev;
        this.copyUrl = copyUrl;
    }

    /**
     * Retrieves the name of the item
     * @return name of the item
     */
    public String getName()
    {
        return name;
    }

    /**
     * Retrieves the url of the item
     * @return url of the item
     */
    public String getUrl()
    {
        return url;
    }

    /**
     * Retrieves the uuid of the repository
     * @return  uuid of the repository
     */
    public String getUuid()
    {
        return uuid;
    }

    /**
     * Retrieves the url of the repository
     * @return url of the repository
     */
    public String getRepository()
    {
        return repository;
    }

    /**
     * Retrieves the schedule of the next commit
     * @return schedule of the next commit
     */
    public int getSchedule()
    {
        return schedule;
    }

    /**
     * Retrieves the nodeKind
     * @return nodeKind
     */
    public int getNodeKind()
    {
        return nodeKind;
    }

    /**
     * Retrieves the author of the last commit
     * @return author of the last commit
     */
    public String getAuthor()
    {
        return author;
    }

    /**
     * Retrieves the last revision the item was updated to
     * @return last revision the item was updated to
     */
    public long getRevision()
    {
        return revision;
    }

    /**
     * Retrieves the revision of the last commit
     * @return the revision of the last commit
     */
    public long getLastChangedRevision()
    {
        return lastChangedRevision;
    }

    /**
     * Retrieves the date of the last commit
     * @return the date of the last commit
     */
    public Date getLastChangedDate()
    {
        return lastChangedDate;
    }

    /**
     * Retrieves the last date the text content was changed
     * @return last date the text content was changed
     */
    public Date getLastDateTextUpdate()
    {
        return lastDateTextUpdate;
    }

    /**
     * Retrieves the last date the properties were changed
     * @return last date the properties were changed
     */
    public Date getLastDatePropsUpdate()
    {
        return lastDatePropsUpdate;
    }

    /**
     * Retrieve if the item was copied
     * @return the item was copied
     */
    public boolean isCopied()
    {
        return copied;
    }

    /**
     * Retrieve if the item was deleted
     * @return the item was deleted
     */
    public boolean isDeleted()
    {
        return deleted;
    }

    /**
     * Retrieve if the item is absent
     * @return the item is absent
     */
    public boolean isAbsent()
    {
        return absent;
    }

    /**
     * Retrieve if the item is incomplete
     * @return the item is incomplete
     */
    public boolean isIncomplete()
    {
        return incomplete;
    }

    /**
     * Retrieves the copy source revision
     * @return copy source revision
     */
    public long getCopyRev()
    {
        return copyRev;
    }

    /**
     * Retrieves the copy source url
     * @return copy source url
     */
    public String getCopyUrl()
    {
        return copyUrl;
    }
}
