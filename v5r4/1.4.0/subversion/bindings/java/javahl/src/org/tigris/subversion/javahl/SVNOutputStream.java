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

import java.io.*;
/**
 * This class connects a java.io.PipedOutputStream to a InputInterface.
 * The outherside of the Pipe must written by another thread, or deadlocks
 * will occure
 */
public class SVNOutputStream extends PipedOutputStream
{
    /**
     * my connection to receive data into subversion
     */
    Inputer myInputer;
    /**
     * Creates a SVNOutputStream so that it is connected with an internal
     * PipedInputStream
     * @throws IOException
     */
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
    /**
     * Get the Interface to connect to SVNAdmin
     * @return the connetion interface
     */
    public InputInterface getInputer()
    {
        return myInputer;
    }
    /**
     * this class implements the connection to SVNAdmin
     */
    public class Inputer implements InputInterface
    {
        /**
         * my side of the pipe
         */
        PipedInputStream myStream;
        /**
         * flag that the other side of the pipe has been closed
         */
        boolean closed;
        /**
         * build a new connection object
         * @param myMaster  the other side of the pipe
         * @throws IOException
         */
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
