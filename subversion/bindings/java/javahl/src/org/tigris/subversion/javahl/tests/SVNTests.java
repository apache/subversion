/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2003-2005 CollabNet.  All rights reserved.
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
package org.tigris.subversion.javahl.tests;

import junit.framework.TestCase;
import junit.framework.TestSuite;
import org.tigris.subversion.javahl.*;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.util.HashMap;
import java.util.Iterator;
import java.util.Map;
/**
 * common base class for the javahl binding tests
 */
public class SVNTests extends TestCase
{
    /**
     * our admin object, mostly used for creating,dumping and loading
     * repositories
     */
    protected SVNAdmin admin;
    /**
     * the subversion client, what we want to test.
     */
    protected SVNClientInterface client;
    /**
     * the root directory. All other files and directories will created in
     * here
     */
    protected File rootDir;
    /**
     * the base name of the test. Together with the testCounter this will make
     * up the directory name of the test.
     */
    protected String testBaseName;
    /**
     * this counter will be incremented for every test in one suite (test class)
     */
    protected static int testCounter;
    /**
     * the file in which the sample repository has been dumped.
     */
    protected File greekDump;
    /**
     * the directory of the sample repository.
     */
    protected File greekRepos;
    /**
     * the initial working copy of the sample repository.
     */
    protected WC greekWC;
    /**
     * the directory "svn-test-work/local_tmp" in the rootDir. This
     * will be used for the sample repository and its dumpfile and for
     * the config directory
     */
    protected File localTmp;
    /**
     * the directory "repositories" in the rootDir. All test repositories will
     * be created here.
     */
    protected File repositories;
    /**
     * the directory "working_copies" in the rootDir. All test working copies
     * will be created here.
     */
    protected File workingCopies;
    /**
     * the directory "config" in the localTmp. It will be used as the
     * configuration directory for all the tests.
     */
    protected File conf;
    /**
     * standard log message. Used for all commits.
     */
    protected String logMessage = "Log Message";
    /**
     * the map of all items expected to be received by the callback for the
     * log message. After each commit, this will be cleared
     */
    protected Map expectedCommitItems;

    /**
     * common root directory for all tests. Can be set by the command line or
     * by the system property "test.rootdir". If not set, the current directory
     * of this process is used
     */
    protected static String rootDirectoryName;
    /**
     * common root URL for all tests. Can be set by the command line or by the
     * system property "test.rooturl". If not set, the file url of the
     * rootDirectoryName is used.
     */
    protected static String rootUrl;

    /**
     * retrieve the root directory and the root url from the command line
     * arguments
     * @param args  command line arguments
     */
    protected static void processArgs(String[] args)
    {
        if (args == null)
            return;
        for (int i = 0; i < args.length; i++)
        {
            String arg = args[i];
            if ("-d".equals(arg))
            {
                if (i + 1 < args.length)
                {
                    rootDirectoryName = args[++i];
                }
            }
            if ("-u".equals(arg))
            {
                if (i + 1 < args.length)
                {
                    rootUrl = args[++i];
                }
            }
        }
    }

    /**
     * Main method, will call all tests of all test classes
     * @param args command line arguments
     */
    public static void main(String[] args)
    {
        processArgs(args);
        junit.textui.TestRunner.run(suite());
    }

    /**
     * build a test suite with all test of all test classes known
     * @return complete test suite
     */
    public static TestSuite suite()
    {
        TestSuite suite = new TestSuite();
        suite.addTestSuite(BasicTests.class);
        suite.addTestSuite(SVNAdminTests.class);
        return suite;
    }

    /**
     * Initialize one test object
     */
    protected SVNTests()
    {
        // if not already set, get a usefull value for rootDir
        if(rootDirectoryName == null)
            rootDirectoryName = System.getProperty("test.rootdir");
        if(rootDirectoryName == null)
            rootDirectoryName = System.getProperty("user.dir");
        rootDir = new File(rootDirectoryName);

        // if not alread set, get a usefull value for root url
        if(rootUrl == null)
            rootUrl = System.getProperty("test.rooturl");
        if(rootUrl == null)
        {
            // if no root url, set build a file url
            rootUrl = rootDir.toURI().toString();
            // java may have a different view about the number of '/' to follow
            // "file:" than subversion. We convert to the subversion view.
            if(rootUrl.startsWith("file:///"))
                ; // this is the form subversion needs
            else if(rootUrl.startsWith("file://"))
                rootUrl = rootUrl.replaceFirst("file://", "file:///");
            else if(rootUrl.startsWith("file:/"))
                rootUrl = rootUrl.replaceFirst("file:/", "file:///");
        }
    }

    /**
     * Standard initialization of one test
     * @throws Exception
     */
    protected void setUp() throws Exception
    {
        super.setUp();

        // create a clean directory for the config files and the sample
        // repository
        //
        // ### The path is now "svn-test-work/local_tmp", however, I'm
        // ### not sure how to update this code for that.
        localTmp = new File(rootDir, "local_tmp");
        if(localTmp.exists())
            removeDirectoryWithContent(localTmp);
        localTmp.mkdir();
        conf = new File(localTmp, "config");
        conf.mkdir();

        // create and configure the needed subversion objects
        admin = new SVNAdmin();
        client = new SVNClientSynchronized();
        client.notification2(new MyNotifier());
        client.commitMessageHandler(new MyCommitMessage());
        client.username("jrandom");
        client.password("rayjandom");
        client.setConfigDirectory(conf.getAbsolutePath());
        expectedCommitItems = new HashMap();

        // build and dump the sample repository
        File greekFiles = buildGreekFiles();
        greekRepos = new File(localTmp, "repos");
        greekDump = new File(localTmp, "greek_dump");
        admin.create(greekRepos.getAbsolutePath(), true,false, null,
                SVNAdmin.BDB);
        addExpectedCommitItem(greekFiles.getAbsolutePath(), null, null,
                NodeKind.none, CommitItemStateFlags.Add);
        client.doImport(greekFiles.getAbsolutePath(), makeReposUrl(greekRepos),
                null, true );
        admin.dump(greekRepos.getAbsolutePath(), new FileOutputer(greekDump),
                new IgnoreOutputer(), null, null, false);

        // create the directory for the repositories and the working copies
        //
        // ### The paths are now "svn-test-work/repositories" and
        // ### "svn-test-work/repositories".  However, I'm not sure
        // ### how to update this code for that. 
        repositories = new File(rootDir, "repositories");
        repositories.mkdirs();
        workingCopies = new File(rootDir, "working_copies");
        workingCopies.mkdirs();
    }

    /**
     * build a sample directory with test files to be used as import for
     * the sample repository. Create also the master working copy test set.
     * @return  the sample repository
     * @throws IOException
     */
    private File buildGreekFiles() throws IOException
    {
        File greekFiles = new File(localTmp, "greek_files");
        greekFiles.mkdir();
        greekWC = new WC();
        greekWC.addItem("",null);
        greekWC.addItem("iota", "This is the file 'iota'.");
        greekWC.addItem("A", null);
        greekWC.addItem("A/mu", "This is the file 'mu'.");
        greekWC.addItem("A/B", null);
        greekWC.addItem("A/B/lambda", "This is the file 'lambda'.");
        greekWC.addItem("A/B/E", null);
        greekWC.addItem("A/B/E/alpha", "This is the file 'alpha'.");
        greekWC.addItem("A/B/E/beta", "This is the file 'beta'.");
        greekWC.addItem("A/B/F", null);
        greekWC.addItem("A/C", null);
        greekWC.addItem("A/D", null);
        greekWC.addItem("A/D/gamma", "This is the file 'gamma'.");
        greekWC.addItem("A/D/H", null);
        greekWC.addItem("A/D/H/chi", "This is the file 'chi'.");
        greekWC.addItem("A/D/H/psi", "This is the file 'psi'.");
        greekWC.addItem("A/D/H/omega", "This is the file 'omega'.");
        greekWC.addItem("A/D/G", null);
        greekWC.addItem("A/D/G/pi", "This is the file 'pi'.");
        greekWC.addItem("A/D/G/rho", "This is the file 'rho'.");
        greekWC.addItem("A/D/G/tau", "This is the file 'tau'.");
        greekWC.materialize(greekFiles);
        return greekFiles;
    }

    /**
     * Remove a directory with all files and directories it may contain.
     * @param localTmp
     */
    protected void removeDirectoryWithContent(File localTmp)
    {
        if(localTmp.isDirectory())
        {
            File[] content = localTmp.listFiles();
            for(int i = 0; i < content.length; i++)
                removeDirectoryWithContent(content[i]);
        }
        localTmp.delete();
    }

    /**
     * cleanup after one test
     * @throws Exception
     */
    protected void tearDown() throws Exception
    {
        // take care of our subversion objects.
        admin.dispose();
        client.dispose();
        // remove the temporary directory
        removeDirectoryWithContent(localTmp);
        super.tearDown();
    }

    /**
     * Create the url for the repository to be used for the tests.
     * @param file  the directory of the repository
     * @return the URL for the repository
     */
    protected String makeReposUrl(File file)
    {
        // split the common part of the root directory
        String path = file.getAbsolutePath().
                substring(rootDirectoryName.length()+1);
        // append to the root url
        return rootUrl + path.replace(File.separatorChar, '/');
    }

    /**
     * add another commit item expected during the callback for the log message.
     * @param workingCopyPath   the path of the of the working
     * @param baseUrl           the url for the repository
     * @param itemPath          the path of the item relative the working copy
     * @param nodeKind          expected node kind (dir or file or none)
     * @param stateFlags        expected commit state flags
     *                          (see CommitItemStateFlags)
     */
    protected void addExpectedCommitItem(String workingCopyPath, String baseUrl,
                                         String itemPath, int nodeKind,
                                         int stateFlags)
    {
        //determine the full working copy path and the full url of the item.
        String path = null;
        if(workingCopyPath != null)
            if(itemPath != null)
                path = workingCopyPath.replace(File.separatorChar, '/') +
                        '/' + itemPath;
            else
                path = workingCopyPath.replace(File.separatorChar, '/');
        String url = null;
        if(baseUrl != null)
            if(itemPath != null)
                url = baseUrl + '/' + itemPath;
            else
                url = baseUrl;

        // the key of the item is either the url or the path (if no url)
        String key;
        if(url != null)
            key = url;
        else
            key = path;
        expectedCommitItems.put(key, new MyCommitItem(path, nodeKind,
                stateFlags, url));
    }

    /**
     * Intended to be called as part of test method execution
     * (post-{@link #setUp()}).  Calls <code>fail()</code> if the
     * directory name cannot be determined.
     *
     * @return The name of the working copy administrative directory.
     * @since 1.3
     */
    protected String getAdminDirectoryName() {
        String admDirName = null;
        if (this.client != null) {
            admDirName = client.getAdminDirectoryName();
        }
        if (admDirName == null) {
            fail("Unable to determine the WC admin directory name");
        }
        return admDirName;
    }

    /**
     * internal class which implements the OutputInterface to write the data
     * to a file.
     */
    public class FileOutputer implements OutputInterface
    {
        /**
         * the output file stream
         */
        FileOutputStream myStream;
        /**
         * create new object
         * @param outputName    the file to write the data to
         * @throws IOException
         */
        public FileOutputer(File outputName) throws IOException
        {
            myStream = new FileOutputStream(outputName);
        }

        /**
         * write the bytes in data to java
         * @param data          the data to be writtem
         * @throws IOException  throw in case of problems.
         */
        public int write(byte[] data) throws IOException
        {
            myStream.write(data);
            return data.length;
        }

        /**
         * close the output
         * @throws IOException throw in case of problems.
         */
        public void close() throws IOException
        {
            myStream.close();
        }
    }

    /**
     * internal class implements the OutputInterface, but ignores the data
     */
    public class IgnoreOutputer implements OutputInterface
    {
        /**
         * write the bytes in data to java
         * @param data          the data to be writtem
         * @throws IOException  throw in case of problems.
         */
        public int write(byte[] data) throws IOException
        {
            return data.length;
        }

        /**
         * close the output
         * @throws IOException throw in case of problems.
         */
        public void close() throws IOException
        {
        }
    }

    /**
     * internal class which implements the InputInterface to read the data
     * from a file.
     */
    public class FileInputer implements InputInterface
    {
        /**
         * input file stream
         */
        FileInputStream myStream;
        /**
         * create a new object
         * @param inputName     the file from which the data is read
         * @throws IOException
         */
        public FileInputer(File inputName) throws IOException
        {
            myStream = new FileInputStream(inputName);
        }

        /**
         * read the number of data.length bytes from input.
         * @param data          array to store the read bytes.
         * @throws IOException  throw in case of problems.
         */
        public int read(byte[] data) throws IOException
        {
            return myStream.read(data);
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

    /**
     * this internal class represent the repository and the working copy for
     * one test.
     */
    protected class OneTest
    {
        /**
         * the file name of repository (used by SVNAdmin)
         */
        protected File repository;
        /**
         * the file name of the working copy directory
         */
        protected File workingCopy;
        /**
         * the url of the repository (used by SVNClient)
         */
        protected String url;
        /**
         * the expected layout of the working copy after the next subversion
         * command
         */
        protected WC wc;
        /**
         * build a new test setup with a new repository, a new working and a
         * new expected working layout
         * @throws Exception
         */
        protected OneTest() throws Exception
        {
            String testName = testBaseName + ++testCounter;
            wc = greekWC.copy();
            repository = createStartRepository(testName);
            url = makeReposUrl(repository);
            workingCopy = createStartWorkingCopy(repository, testName);
        }
        /**
         * Copy the working copy and the expected working copy layout for tests
         * which need multiple working copy
         * @param append    append to the working copy name of the original
         * @return second test object.
         * @throws Exception
         */
        protected OneTest copy(String append) throws Exception
        {
            return new OneTest(this, append);
        }
        /**
         * constructor for create a copy
         * @param orig      original test
         * @param append    append this to the directory name of the original
         *                  test
         * @throws Exception
         */
        private OneTest(OneTest orig, String append) throws Exception
        {
            String testName = testBaseName + testCounter +append;
            repository = orig.getRepository();
            url = orig.getUrl();
            wc = orig.wc.copy();
            workingCopy = createStartWorkingCopy(repository, testName);
        }
        /**
         * Return the directory of the repository
         * @return the repository directory name
         */
        public File getRepository()
        {
            return repository;
        }
        /**
         * Return the name of the directory of the repository
         * @return the name of repository directory
         */
        public String getRepositoryPath()
        {
            return repository.getAbsolutePath();
        }
        /**
         * Return the working copy directory
         * @return the working copy directory
         */
        public File getWorkingCopy()
        {
            return workingCopy;
        }
        /**
         * Return the working copy directory name
         * @return the name of the working copy directory
         */
        public String getWCPath()
        {
            return workingCopy.getAbsolutePath();
        }
        /**
         * Returns the url of repository
         * @return  the url
         */
        public String getUrl()
        {
            return url;
        }
        /**
         * Returns the expected working copy content
         * @return the expected working copy content
         */
        public WC getWc()
        {
            return wc;
        }
        /**
         * Create the repository for the beginning of the test
         * @param testName      the name of the test
         * @return  the repository directory
         * @throws Exception
         */
        protected File createStartRepository(String testName) throws Exception
        {
            // build a clean repository directory
            File repos = new File(repositories, testName);
            removeDirectoryWithContent(repos);
            // create and load the repository from the default repository dump
            admin.create(repos.getAbsolutePath(), true, false,
                    conf.getAbsolutePath(), SVNAdmin.BDB);
            admin.load(repos.getAbsolutePath(), new FileInputer(greekDump),
                    new IgnoreOutputer(), false, false, null);
            return repos;
        }
        /**
         * Create the working copy for the beginning of the test
         * @param repos     the repository directory
         * @param testName  the name of the test
         * @return the directory of the working copy
         * @throws Exception
         */
        protected File createStartWorkingCopy(File repos, String testName)
                throws Exception
        {
            // build a clean working directory
            String uri = makeReposUrl(repos);
            workingCopy = new File(workingCopies, testName);
            removeDirectoryWithContent(workingCopy);
            // checkout the repository
            client.checkout(uri, workingCopy.getAbsolutePath(), null, true);
            // sanity check the working with its expected status
            checkStatus();
            return workingCopy;
        }
        /**
         * Check if the working copy has the expected status
         * @throws Exception
         */
        public void checkStatus() throws Exception
        {
            Status[] states = client.status(workingCopy.getAbsolutePath(), true,
                    false, true, true);
            wc.check(states, workingCopy.getAbsolutePath());
        }
    }

    /**
     * internal class to receive the request for the log messages to check if
     * the expected commit items are presented
     */
    class MyCommitMessage implements CommitMessage
    {
        /**
         * Retrieve a commit message from the user based on the items to be commited
         * @param elementsToBeCommited  Array of elements to be commited
         * @return  the log message of the commit.
         */
        public String getLogMessage(CommitItem[] elementsToBeCommited)
        {
            // check all received CommitItems are expected as received
            for (int i = 0; i < elementsToBeCommited.length; i++)
            {
                CommitItem commitItem = elementsToBeCommited[i];
                // since imports do not provide a url, the key is either url or
                // path
                String key;
                if(commitItem.getUrl() != null)
                    key = commitItem.getUrl();
                else
                    key = commitItem.getPath();

                MyCommitItem myItem = (MyCommitItem) expectedCommitItems.get(
                        key);
                // check commit item is expected and has the expected data
                assertNotNull("commit item for "+key+ " not expected", myItem);
                myItem.test(commitItem, key);
            }

            // all remaining expected commit items are missing
            for (Iterator iterator = expectedCommitItems.keySet().iterator();
                 iterator.hasNext();)
            {
                String str =  (String) iterator.next();
                fail("commit item for "+str+" not found");
            }
            return logMessage;
        }
    }

    /**
     * internal class to describe an expected commit item
     */
    class MyCommitItem
    {
        /**
         * the path of the item
         */
        String myPath;
        /**
         * the kind of node (file, directory or none, see NodeKind)
         */
        int myNodeKind;
        /**
         * the reason why this item is commited (see CommitItemStateFlag)
         */
        int myStateFlags;
        /**
         * the url of the item
         */
        String myUrl;
        /**
         * build one expected commit item
         * @param path          the expected path
         * @param nodeKind      the expected node kind
         * @param stateFlags    the expected state flags
         * @param url           the expected url
         */
        private MyCommitItem(String path, int nodeKind, int stateFlags,
                             String url)
        {
            myPath = path;
            myNodeKind = nodeKind;
            myStateFlags = stateFlags;
            myUrl = url;
        }
        /**
         * Check if the commit item has the expected data
         * @param ci    the commit item to check
         * @param key   the key of the item
         */
        private void test(CommitItem ci, String key)
        {
            assertEquals("commit item path", ci.getPath(), myPath);
            assertEquals("commit item node kind", ci.getNodeKind(), myNodeKind);
            assertEquals("commit item state flags", ci.getStateFlags(),
                    myStateFlags);
            assertEquals("commit item url", ci.getUrl(), myUrl);
            // after the test, remove the item from the expected map
            expectedCommitItems.remove(key);
        }
    }

    class MyNotifier implements Notify2
    {

        /**
         * Handler for Subversion notifications.
         * <p/>
         * Override this function to allow Subversion to send notifications
         *
         * @param info everything to know about this event
         */
        public void onNotify(NotifyInformation info)
        {
        }
    }
}
