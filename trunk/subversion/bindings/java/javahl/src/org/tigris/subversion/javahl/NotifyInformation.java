/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
/**
 * this class contains all the information passed by the onNotify2 method of
 * the Notify2 class. This is used notify the SVNClientInterfacce users all
 * relevant events.
 * @since 1.2
 */
public class NotifyInformation
{
    /**
     * the path of the item, which is the source of the event.
     */
    private String path;
    /**
     * the action, which triggered this event (See NotifyAction).
     */
    private int action;
    /**
     * the kind of the item (See NodeKind).
     */
    private int kind;
    /**
     * the mime type of the item.
     */
    private String mimeType;
    /**
     * any lock for the item
     */
    private Lock lock;
    /**
     * any error message for the item
     */
    private String errMsg;
    /**
     * the state of the content of the item (See NotifyStatus).
     */
    private int contentState;
    /**
     * the state of the properties of the item (See NotifyStatus).
     */
    private int propState;
    /**
     * the state of the lock of the item (See LockStatus).
     */
    private int lockState;
    /**
     * the revision of the item.
     */
    private long revision;

    /**
     * This constructor is to be used by the native code. For the parameter
     * see the matching members
     * @param path
     * @param action
     * @param kind
     * @param mimeType
     * @param lock
     * @param errMsg
     * @param contentState
     * @param propState
     * @param lockState
     * @param revision
     */
    NotifyInformation(String path, int action, int kind, String mimeType, Lock lock, String errMsg, int contentState, int propState, int lockState, long revision)
    {
        this.path = path;
        this.action = action;
        this.kind = kind;
        this.mimeType = mimeType;
        this.lock = lock;
        this.errMsg = errMsg;
        this.contentState = contentState;
        this.propState = propState;
        this.lockState = lockState;
        this.revision = revision;
    }

    /**
     * return the path of the item, which is the source of the event.
     */
    public String getPath()
    {
        return path;
    }

    /**
     * return the action, which triggered this event (See NotifyAction).
     */
    public int getAction()
    {
        return action;
    }

    /**
     * return the kind of the item (See NodeKind).
     */
    public int getKind()
    {
        return kind;
    }

    /**
     * return the mime type of the item.
     */
    public String getMimeType()
    {
        return mimeType;
    }

    /**
     * return any lock for the item
     */
    public Lock getLock()
    {
        return lock;
    }

    /**
     * return any error message for the item
     */
    public String getErrMsg()
    {
        return errMsg;
    }

    /**
     * return the state of the content of the item (See NotifyStatus).
     */
    public int getContentState()
    {
        return contentState;
    }

    /**
     * return the state of the properties of the item (See NotifyStatus).
     */
    public int getPropState()
    {
        return propState;
    }

    /**
     * return the state of the lock of the item (See LockStatus).
     */
    public int getLockState()
    {
        return lockState;
    }

    /**
     * return the revision of the item.
     */
    public long getRevision()
    {
        return revision;
    }
}
