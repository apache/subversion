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
 * Subversion client exception class.
 * This exception is throw whenever something goes wrong in the jni-interface
 */
public class ClientException extends Exception
{
    /**
     * The constructor is only used by the native library.
     * @param message message 
     * @param source source
     * @param aprError APR error code
     */
    ClientException(String message, String source, int aprError)
    {
    	super(message);
        this.source = source;
		this.aprError = aprError;
    }
    /**
     * the exception message
     */
    private String message;

    /**
     * the error source
     */
    private String source;
    /**
     * the APR error id
     */
    private int aprError;

    /**
     * Returns the error source.
     */
    public String getSource()
    {
        return source;
    }
    /**
     * Returns the APR error id.
     */
    public int getAprError()
    {
        return aprError;
    }
}
