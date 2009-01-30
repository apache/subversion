/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
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
 * Poor mans enum for svn_wc_operation_t
 */
public final class Operation
{
    /* none */
    public static final int none = 0;

    /* update */
    public static final int update = 1;

    /* switch */
    /* Note: this is different that svn_wc.h, because 'switch' is a
     * reserved word in java  :(  */
    public static final int switched = 2;

    /* merge */
    public static final int merge = 3;
}
