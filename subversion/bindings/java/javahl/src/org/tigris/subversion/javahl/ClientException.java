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
 * This exception is thrown whenever something goes wrong in the
 * Subversion JavaHL binding's JNI interface.
 */
public class ClientException extends Exception
{
    /**
     * Any associated error source (e.g. line number) for a wrapped
     * <code>svn_error_t</code>.
     */
    private String source;

    /**
     * Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    private int aprError;

    /**
     * This constructor is only used by the native library.
     *
     * @param message A description of the problem.
     * @param source The error's source.
     * @param aprError Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    ClientException(String message, String source, int aprError)
    {
        super(message);
        this.source = source;
        this.aprError = aprError;
    }

    /**
     * @return The error source (e.g. line number).
     */
    public String getSource()
    {
        return source;
    }

    /**
     * @return Any associated APR error code for a wrapped
     * <code>svn_error_t</code>.
     */
    public int getAprError()
    {
        return aprError;
    }
}
