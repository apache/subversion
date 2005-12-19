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
/**
 * This class offers the same commands as the svnadmin commandline client
 */
public class SVNAdmin
{
    /**
     * Load the required native library.
     */
    static
    {
        NativeResources.loadNativeLibrary();
    }

    /**
     * Standard empty contructor, builds just the native peer.
     */
    public SVNAdmin()
    {
        cppAddr = ctNative();
    }
    /**
     * Build the native peer
     * @return the adress of the peer
     */
    private native long ctNative();
     /**
     * release the native peer (should not depend on finalize)
     */
    public native void dispose();
    /**
     * release the native peer (should use dispose instead)
     */
    protected native void finalize();
    /**
     * slot for the adress of the native peer. The JNI code is the only user
     * of this member
     */
    protected long cppAddr;
    /**
     * Filesystem in a Berkeley DB
     */
    public static final String BDB = "bdb";
    /**
     * Filesystem in the filesystem
     */
    public static final String FSFS = "fsfs";

    /**
     * @return Version information about the underlying native libraries.
     */
    public Version getVersion()
    {
        return NativeResources.version;
    }

    /**
     * create a subversion repository.
     * @param path                  the path where the repository will been 
     *                              created.
     * @param disableFsyncCommit    disable to fsync at the commit (BDB).
     * @param keepLog               keep the log files (BDB).
     * @param configPath            optional path for user configuration files.
     * @param fstype                the type of the filesystem (BDB or FSFS)
     * @throws ClientException  throw in case of problem
     */
    public native void create(String path, boolean disableFsyncCommit, 
                              boolean keepLog, String configPath,
                              String fstype) throws ClientException;
    /**
     * deltify the revisions in the repository
     * @param path              the path to the repository
     * @param start             start revision
     * @param end               end revision
     * @throws ClientException  throw in case of problem
     */
    public native void deltify(String path, Revision start, Revision end)
            throws ClientException;
    /**
     * dump the data in a repository
     * @param path              the path to the repository
     * @param dataOut           the data will be outputed here
     * @param errorOut          the messages will be outputed here
     * @param start             the first revision to be dumped
     * @param end               the last revision to be dumped
     * @param incremental       the dump will be incremantal
     * @throws ClientException  throw in case of problem
     */
    public native void dump(String path, OutputInterface dataOut,
                            OutputInterface errorOut, Revision start,
                            Revision end, boolean incremental)
            throws ClientException;
    /**
     * make a hot copy of the repository
     * @param path              the path to the source repository
     * @param targetPath        the path to the target repository
     * @param cleanLogs         clean the unused log files in the source
     *                          repository
     * @throws ClientException  throw in case of problem
     */
    public native void hotcopy(String path, String targetPath,
                               boolean cleanLogs) throws ClientException;

    /**
     * list all logfiles (BDB) in use or not)
     * @param path              the path to the repository
     * @param receiver          interface to receive the logfile names
     * @throws ClientException  throw in case of problem
     */
    public native void listDBLogs(String path, MessageReceiver receiver)
            throws ClientException;
    /**
     * list unused logfiles
     * @param path              the path to the repository
     * @param receiver          interface to receive the logfile names
     * @throws ClientException  throw in case of problem
     */
    public native void listUnusedDBLogs(String path, MessageReceiver receiver)
            throws ClientException;

    /**
     * interface to receive the messages
     */
    public static interface MessageReceiver
    {
        /**
         * receive one message line
         * @param message   one line of message
         */
        public void receiveMessageLine(String message);
    }
    /**
     * load the data of a dump into a repository,
     * @param path              the path to the repository
     * @param dataInput         the data input source
     * @param messageOutput     the target for processing messages
     * @param ignoreUUID        ignore any UUID found in the input stream
     * @param forceUUID         set the repository UUID to any found in the
     *                          stream
     * @param relativePath      the directory in the repository, where the data
     *                          in put optional.
     * @throws ClientException  throw in case of problem
     */
    public native void load(String path, InputInterface dataInput,
                            OutputInterface messageOutput, boolean ignoreUUID,
                            boolean forceUUID, String relativePath)
            throws ClientException;

    /**
     * list all open transactions in a repository
     * @param path              the path to the repository
     * @param receiver          receives one transaction name per call
     * @throws ClientException  throw in case of problem
     */
    public native void lstxns(String path, MessageReceiver receiver)
            throws ClientException;
    /**
     * recover the berkeley db of a repository, returns youngest revision
     * @param path              the path to the repository
     * @throws ClientException  throw in case of problem
     */
    public native long recover(String path) throws ClientException;
    /**
     * remove open transaction in a repository
     * @param path              the path to the repository
     * @param transactions      the transactions to be removed
     * @throws ClientException  throw in case of problem
     */
    public native void rmtxns(String path, String [] transactions)
            throws ClientException;
    /**
     * set the log message of a revision
     * @param path              the path to the repository
     * @param rev               the revision to be changed
     * @param message           the message to be set
     * @param bypassHooks       if to bypass all repository hooks
     * @throws ClientException  throw in case of problem
     */
    public native void setLog(String path, Revision rev, String message,
                              boolean bypassHooks)
            throws ClientException;
    /**
     * verify the repository
     * @param path              the path to the repository
     * @param messageOut        the receiver of all messages
     * @param start             the first revision
     * @param end               the last revision
     * @throws ClientException  throw in case of problem
     */
    public native void verify(String path,  OutputInterface messageOut,
                              Revision start, Revision end)
            throws ClientException;

    /**
     * list all locks in the repository
     * @param path              the path to the repository
     * @throws ClientException  throw in case of problem
     * @since 1.2
     */ 
    public native Lock[] lslocks(String path) throws ClientException;
    /**
     * remove multiple locks from the repository
     * @param path              the path to the repository
     * @param locks             the name of the locked items
     * @throws ClientException  throw in case of problem
     * @since 1.2
     */
    public native void rmlocks(String path, String [] locks)
            throws ClientException;
}
