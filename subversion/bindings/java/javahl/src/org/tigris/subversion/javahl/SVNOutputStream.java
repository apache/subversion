package org.tigris.subversion.javahl;

import java.io.*;

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
public class SVNOutputStream extends PipedOutputStream
{
    Inputer myInputer;
    public SVNOutputStream() throws IOException
    {
        myInputer = new Inputer(this);
    }

    /**
     * Closes this piped output stream and releases any system resources
     * associated with this stream. This stream may no longer be used for
     * writing bytes.
     *
     * @exception  IOException  if an I/O error occurs.
     */
    public void close() throws IOException
    {
        myInputer.closed = true;
        super.close();
    }

    public static class Inputer implements InputInterface
    {
        PipedInputStream myStream;
        boolean closed;

        Inputer(SVNOutputStream myMaster) throws IOException
        {
            myStream = new PipedInputStream(myMaster);
        }
        /**
         * read the number of data.length bytes from input.
         * @param data          array to store the read bytes.
         * @throws IOException  throw in case of problems.
         */
        public int read(byte[] data) throws IOException
        {
            if(closed)
                throw new IOException("stream has been closed");
            return myStream.read();
        }

        /**
         * close the input
         * @throws IOException throw in case of problems.
         */
        public void close() throws IOException
        {
            myStream.close();
        }
    }
}
