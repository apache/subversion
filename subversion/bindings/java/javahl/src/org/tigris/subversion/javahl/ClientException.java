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

/**
 * Subversion client exception class.
 * This exception is throw whenever something goes wrong in the jni-interface
 */
public class ClientException extends Exception
{
    /**
     * The constructor is only used by the native library.
     * @param m message
     * @param d description
     * @param s source
     * @param a APR error code
     */
    ClientException(String m, String d, String s, int a)
    {
        super(m);
        de = d;
        so = s;
        ap = a;
    }
    /**
     * the error message
     */
    private String de;
    /**
     * the error source
     */
    private String so;
    /**
     * the APR error id
     */
    private int ap;
    /**
     * Returns the error message.
     */
    public String getDescription()
    {
        return de;
    }
    /**
     * Returns the error source.
     */
    public String getSource()
    {
        return so;
    }
    /**
     * Returns the APR error id.
     */
    public int getAprError()
    {
        return ap;
    }
}
