/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2004 CollabNet.  All rights reserved.
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

import java.io.IOException;

/**
 * Interface for data to be received from subversion
 * used for SVNAdmin.load and SVNAdmin.dump
 */
public interface InputInterface
{
    /**
     * read the number of data.length bytes from input.
     * @param data          array to store the read bytes.
     * @throws IOException  throw in case of problems.
     */
    public int read(byte [] data) throws IOException;

    /**
     * close the input
     * @throws IOException throw in case of problems.
     */
    public void close() throws IOException;
}
