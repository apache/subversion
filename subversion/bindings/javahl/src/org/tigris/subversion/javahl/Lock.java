/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005,2007 CollabNet.  All rights reserved.
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
 * @endcopyright
 */

package org.tigris.subversion.javahl;

import java.util.Date;

/**
 * Class to describe a lock.  It is returned by the lock operation.
 * @since 1.2
 */
public class Lock implements java.io.Serializable
{
    // Update the serialVersionUID when there is a incompatible change
    // made to this class.  See any of the following, depending upon
    // the Java release.
    // http://java.sun.com/j2se/1.3/docs/guide/serialization/spec/version.doc7.html
    // http://java.sun.com/j2se/1.4/pdf/serial-spec.pdf
    // http://java.sun.com/j2se/1.5.0/docs/guide/serialization/spec/version.html#6678
    // http://java.sun.com/javase/6/docs/platform/serialization/spec/version.html#6678
    private static final long serialVersionUID = 1L;

    /**
     * the owner of the lock
     */
    private String owner;

    /**
     * the path of the locked item
     */
    private String path;

    /**
     * the token provided during the lock operation
     */
    private String token;

    /**
     * the comment provided during the lock operation
     */
    private String comment;

    /**
     * the date when the lock was created
     */
    private long creationDate;

    /**
     * the date when the lock will expire
     */
    private long expirationDate;

    /**
     * this constructor should only called from JNI code
     * @param owner             the owner of the lock
     * @param path              the path of the locked item
     * @param token             the lock token
     * @param comment           the lock comment
     * @param creationDate      the date when the lock was created
     * @param expirationDate    the date when the lock will expire
     */
    Lock(String owner, String path, String token, String comment,
         long creationDate, long expirationDate)
    {
        this.owner = owner;
        this.path = path;
        this.token = token;
        this.comment = comment;
        this.creationDate = creationDate;
        this.expirationDate = expirationDate;
    }

    /**
     * @return the owner of the lock
     */
    public String getOwner()
    {
        return owner;
    }

    /**
     * @return the path of the locked item
     */
    public String getPath()
    {
        return path;
    }

    /**
     * @return the token provided during the lock operation
     */
    public String getToken()
    {
        return token;
    }

    /**
     * @return the comment provided during the lock operation
     */
    public String getComment()
    {
        return comment;
    }

    /**
     * @return the date the lock was created
     */
    public Date getCreationDate()
    {
        if (creationDate == 0)
            return null;
        else
            return new Date(creationDate/1000);
    }

    /**
     * @return the date when the lock will expire
     */
    public Date getExpirationDate()
    {
        if (expirationDate == 0)
            return null;
        else
            return new Date(expirationDate/1000);
    }

}
