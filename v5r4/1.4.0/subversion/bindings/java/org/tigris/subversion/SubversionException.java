package org.tigris.subversion;

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
 */

/**
 * Corresponds to svn_error_t from the C API.
 */
public class SubversionException extends Exception {

    /* Throwable which caused this SubversionException or null */
    private Throwable cause;

    /* APR error value, possibly SVN_ custom err */
    private long status;

    /* Source file where the error originated. Only used iff SVN_DEBUG */
    private String file;

    /* Source line where the error originated. Only used iff SVN_DEBUG */
    private long line;

    /**
     * Creates a new instance.
     * @param message
     */
    public SubversionException(String message) {
        super(message);
    }

    /**
     * Creates a new instance.
     * @param message
     * @param cause
     * @param status
     * @param file
     * @param line
     */
    public SubversionException(String message, Throwable cause, long status,
                               String file, long line) {
        super(message);
        this.cause = cause;
        this.status = status;
        this.file = file;
        this.line = line;
    }

    /**
     * Returns the cause of this SubversionException.
     * @return java.lang.Throwable
     */
    public Throwable getCause() {
        return cause;
    }

    /**
     * Returns the detail message string of this SubversionException.
     * @see java.lang.Throwable#getMessage()
     */
    public String getMessage() {
        String msg = super.getMessage();
        if (getFile() != null) {
            msg = getFile() + ':' + getLine() + '(' + getStatus() + ") " + msg;
        }
        return msg;
    }

    /**
     * @return The source file where the error originated.
     */
    public String getFile() {
        return file;
    }

    /**
     * @return The source line where the error originated.
     */
    public long getLine() {
        return line;
    }

    /**
     * @return APR error value, possibly SVN_ custom err.
     */
    public long getStatus() {
        return status;
    }
}
