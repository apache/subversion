package org.tigris.subversion.test;

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

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.FileInputStream;

import junit.framework.TestCase;
import junit.framework.TestSuite;

//import org.tigris.subversion...;

public class SVNTests extends TestCase
{
    /*
    protected SVNAdmin admin;
    protected SVNClient client;
    */
    protected String urlSchema = "file:///";
    protected File rootDir = new File(System.getProperty("user.dir"));
    protected String testBaseName;
    protected static int testCounter;
    protected File greekDump;
    protected File greekRepos;
    //protected WC greekWC;
    protected File localTmp;
    protected File repositories;
    protected File workingCopies;
    protected File conf;

    public SVNTests(String name)
    {
        super(name);
        testBaseName = name;
    }

    protected void setUp() throws Exception
    {
        super.setUp();
//         admin = new SVNAdmin();
//         client = new SVNClient();
//         client.notification(new Notify() {
//            /**
//             * Handler for Subversion notifications.
//             *
//             * Override this function to allow Subversion to
//             * send notifications
//             * @param path on which action happen
//             * @param action subversion action, see svn_wc_notify_action_t
//             * @param kind node kind of path after action occurred
//             * @param mimeType mime type of path after action occurred
//             * @param contentState state of content after action occurred
//             * @param propState state of properties after action occurred
//             * @param revision revision number  after action occurred
//             */
//            public void onNotify(String path, int action, int kind,
//                                 String mimeType, int contentState,
//                                 int propState, long revision)
//            {
//                System.out.println(path + ' ' + action + ' ' + kind + ' ' +
//                                   mimeType + ' ' + contentState + ' ' +
//                                   propState + ' ' + revision);
//            }
//         });
        localTmp = new File(rootDir, "local_tmp");
        if (localTmp.exists())
        {
            removeDirectoryWithContent(localTmp);
        }
        localTmp.mkdir();
        conf = new File(localTmp, "config");
        conf.mkdir();
        File greekFiles = buildGreekFiles();
        greekRepos = new File(localTmp, "repos");
        greekDump = new File(localTmp, "greek_dump");
        repositories = new File(rootDir, "repositories");
        repositories.mkdirs();
        workingCopies = new File(rootDir, "working_copies");
        workingCopies.mkdirs();
        /*
        admin.create(greekRepos.getAbsolutePath(), true, false, null);
        client.username("jrandom");
        client.password("rayjandom");
        client.setConfigDirectory(conf.getAbsolutePath());
        client.doImport(greekFiles.getAbsolutePath(), makeReposUrl(greekRepos),
                        "Log message for revision 1.", true);
        admin.dump(greekRepos.getAbsolutePath(), new FileOutputer(greekDump),
                   new IgnoreOutputer(), null, null, false);
        */
    }

    private File buildGreekFiles() throws IOException
    {
        File greekFiles = new File(localTmp, "greek_files");
        greekFiles.mkdir();
        /*
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
        */
        return greekFiles;
    }

    protected void removeDirectoryWithContent(File localTmp)
    {
        if (localTmp.isDirectory())
        {
            File[] content = localTmp.listFiles();
            for (int i = 0; i < content.length; i++)
            {
                removeDirectoryWithContent(content[i]);
            }
        }
        localTmp.delete();
    }

    protected void tearDown() throws Exception
    {
        /*
        admin.dispose();
        //client.notification(null);
        client.dispose();
        */
        removeDirectoryWithContent(localTmp);
        super.tearDown();
    }

    protected String makeReposURL(File file)
    {
        return urlSchema + file.getAbsolutePath().replace(File.separatorChar,
                                                          '/');
    }

//     public class FileOutputer implements OutputInterface
//     {
//         FileOutputStream myStream;
//         public FileOutputer(File outputName) throws IOException
//         {
//             myStream = new FileOutputStream(outputName);
//         }

//         /**
//          * write the bytes in data to java
//          * @param data          the data to be writtem
//          * @throws IOException  throw in case of problems.
//          */
//         public int write(byte[] data) throws IOException
//         {
//             myStream.write(data);
//             return data.length;
//         }

//         /**
//          * close the output
//          * @throws IOException throw in case of problems.
//          */
//         public void close() throws IOException
//         {
//             myStream.close();
//         }
//     }

//     public class IgnoreOutputer implements OutputInterface
//     {
//         /**
//          * write the bytes in data to java
//          * @param data          the data to be writtem
//          * @throws IOException  throw in case of problems.
//          */
//         public int write(byte[] data) throws IOException
//         {
//             return data.length;
//         }

//         /**
//          * close the output
//          * @throws IOException throw in case of problems.
//          */
//         public void close() throws IOException
//         {
//         }
//     }

//     public class FileInputer implements InputInterface
//     {
//         FileInputStream myStream;
//         public FileInputer(File inputName) throws IOException
//         {
//             myStream = new FileInputStream(inputName);
//         }

//         /**
//          * read the number of data.length bytes from input.
//          * @param data          array to store the read bytes.
//          * @throws IOException  throw in case of problems.
//          */
//         public int read(byte[] data) throws IOException
//         {
//             return myStream.read(data);
//         }

//         /**
//          * close the input
//          * @throws IOException throw in case of problems.
//          */
//         public void close() throws IOException
//         {
//             myStream.close();
//         }
//     }

    public class OneTest
    {
        public File getRepository()
        {
            return repository;
        }

        public File getWorkingCopy()
        {
            return workingCopy;
        }

        public String getURL()
        {
            return url;
        }

        protected File repository;
        protected File workingCopy;
        protected String url;
        //protected WC wc;

        /*
        public WC getWc()
        {
            return wc;
        }
        */

        public OneTest copy(String append)
            throws Exception
        {
            return new OneTest(this, append);
        }

        OneTest(OneTest orig, String append)
            throws Exception
        {
            String testName = testBaseName + testCounter +append;
            //wc = orig.wc.copy();
            repository = orig.getRepository();
            url = orig.getURL();
            workingCopy = createStartWorkingCopy(repository, testName);
        }

        public OneTest()
            throws Exception
        {
            String testName = testBaseName + ++testCounter;
            //wc = greekWC.copy();
            repository = createStartRepository(testName);
            url = makeReposURL(repository);
            workingCopy = createStartWorkingCopy(repository, testName);
        }

        protected File createStartRepository(String testName)
            throws Exception
        {
            File repos = new File(repositories, testName);
            removeDirectoryWithContent(repos);
            /*
            admin.create(repos.getAbsolutePath(), true, false,
                         conf.getAbsolutePath());
            admin.load(repos.getAbsolutePath(), new FileInputer(greekDump),
                       new IgnoreOutputer(), false, false, null);
            */
            return repos;
        }

        protected File createStartWorkingCopy(File repos, String testName)
            throws Exception
        {
            String url = makeReposURL(repos);
            File workingCopy = new File(workingCopies, testName);
            removeDirectoryWithContent(workingCopy);
            /*
            client.checkout(url, workingCopy.getAbsolutePath(), null, true);
            Status[] states = client.status(workingCopy.getAbsolutePath(),
                                            true, false, true, true);
            wc.check(states, workingCopy.getAbsolutePath());
            */
            return workingCopy;
        }

        public void checkStatus() throws Exception
        {
            /*
            Status[] states = client.status(workingCopy.getAbsolutePath(),
                                            true, false, true, true);
            wc.check(states, workingCopy.getAbsolutePath());
            */
        }
    }
}
