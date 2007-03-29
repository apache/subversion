/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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
 * Subversion JavaHL binding's JNI code.
 */
class NativeException extends SubversionException
{
    /**
     * Any associated error source (e.g. file name and line number)
     * for a wrapped <code>svn_error_t</code>.
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
    NativeException(String message, String source, int aprError)
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

    /**
     * @return The description, with {@link #source} and {@link
     * #aprError} appended (if any).
     */
    public String getMessage()
    {
        StringBuffer msg = new StringBuffer(super.getMessage());
        // ### This might be better off in JNIUtil::handleSVNError().
        String src = getSource();
        if (src != null)
        {
            msg.append("svn: ");
            msg.append(src);
            int aprErr = getAprError();
            if (aprErr != -1)
            {
                msg.append(": (apr_err=").append(aprErr).append(')');
            }
        }
        return msg.toString();
    }
}
