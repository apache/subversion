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
/**
 * this class describes a item which will be commited.
 */
public class CommitItem
{
    /**
     * the pathname of the item to be commit
     */
    String path;
    /**
     * the kind node (file or directory)
     */
    int nodeKind;
    /**
     * the kind of change to be commited (See CommitItemStateFlages)
     */
    int stateFlags;
    /**
     * the url of the item
     */
    String url;
    /**
     * the source of the copy
     */
    String copyUrl;
    /**
     * the revision
     */
    long revision;
    /**
     * This constructor will be only called from the jni code.
     * @param p     path to the commit item
     * @param nk    kind of node (see NodeKind)
     * @param sf    state flags (see StateFlags)
     * @param u     url of the item
     * @param cu    copy source url
     * @param r     revision number
     */
    CommitItem(String p, int nk, int sf, String u, String cu, long r)
    {
        path = p;
        nodeKind = nk;
        stateFlags = sf;
        url = u;
        copyUrl = cu;
        revision = r;
    }
    /**
     *  retrieve the path of the commit item
     * @return the path.
     */
    public String getPath()
    {
        return path;
    }
    /**
     * return the node kind of the commit item
     * @return the node kind. Look at the NodeKind class.
     */
    public int getNodeKind()
    {
        return nodeKind;
    }
    /**
     * return the kind of change for the commit item.
     * @return  the state flags. Look at the CommitItemStateFlags interface.
     */
    public int getStateFlags()
    {
        return stateFlags;
    }
    /**
     * the class for the commit item state flags. The values are ored together
     * the values are CommitItemStateFlags for building reasons.
     */
    public static class StateFlags implements CommitItemStateFlags
    {
    }
    /**
     * Returns the url of the item
     * @return url
     */
    public String getUrl()
    {
        return url;
    }
    /**
     * Returns the source url if the item is copied
     * @return source url
     */
    public String getCopyUrl()
    {
        return copyUrl;
    }
    /**
     * Returns the revision number
     * @return revision number
     */
    public long getRevision()
    {
        return revision;
    }
}
