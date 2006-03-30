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
package org.tigris.subversion.javahl.tests;

import junit.framework.TestSuite;
import junit.framework.TestResult;
import org.tigris.subversion.javahl.*;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;

/**
 * Tests the basic functionality of javahl binding (inspired by the
 * tests in subversion/tests/cmdline/basic_tests.py).
 */
public class BasicTests extends SVNTests
{
    /**
     * base name of all our tests
     */
    public final static String testName = "basic_test";

    /**
     * Initialize the testBaseName and the testCounter, if this is the first
     * test of this class
     */
    public BasicTests()
    {
        if(!testName.equals(testBaseName))
        {
            testCounter = 0;
            testBaseName = testName;
        }
    }

    /**
     * Build a test suite of all tests of this class
     * @return the new test suite
     */
    public static TestSuite suite() {
        return new TestSuite(BasicTests.class);
    }

    /**
     * Main method to run tests standalone
     * @param args command line arguments to specify root directory and root
     * url
     */
    public static void main(String[] args) {
        processArgs(args);
        TestResult tr = junit.textui.TestRunner.run(suite());
        if (tr.errorCount() != 0 || tr.failureCount() != 0) {
            System.exit(1);
        }
        System.exit(0);
    }

    /**
     * Test SVNClient.getVersion().
     * @throws Throwable
     */
    public void testVersion() throws Throwable
    {
        try
        {
            Version version = client.getVersion();
            String versionString = version.toString();
            if (versionString == null || versionString.trim().length() == 0)
            {
                throw new Exception("Version string empty");
            }
        }
        catch (Exception e)
        {
            fail("Version should always be available unless the " +
                 "native libraries failed to initialize: " + e);
        }
    }

    /**
     * Tests Subversion path validation.
     */
    public void testPathValidation() throws Throwable
    {
        // Rather than segfaulting, JavaHL considers null an invalid path.
        assertFalse("Path validation produced false-positive for null path",
                    Path.isValid(null));

        String path = "valid-path";
        assertTrue("Validation check of valid path '" + path +
                   "' should succeed", Path.isValid(path));

        // File names cannot contain control characters.
        path = "invalid-\u0001-path";
        assertFalse("Validation check of invalid path '" + path +
                    "' (which contains control characters) should fail",
                    Path.isValid(path));
    }

    /**
     * test the basic SVNClient.checkout functionality
     * @throws Throwable
     */
    public void testBasicCheckout() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();
        try
        {
            // obstructed checkout must fail
            client.checkout(thisTest.getUrl() + "/A", thisTest.getWCPath(),
                    null, true);
            fail("missing exception");
        }
        catch (ClientException expected)
        {
        }
        // modify file A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);

        // delete A/B/lambda without svn
        File lambda = new File(thisTest.getWorkingCopy(), "A/B/lambda");
        lambda.delete();
        thisTest.getWc().setItemTextStatus("A/B/lambda", Status.Kind.missing);

        // remove A/D/G
        client.remove(new String[]{thisTest.getWCPath() + "/A/D/G"}, null,
                false);
        thisTest.getWc().setItemTextStatus("A/D/G", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/pi", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/tau", Status.Kind.deleted);

        // check the status of the working copy
        thisTest.checkStatus();

        // recheckout the working copy
        client.checkout(thisTest.getUrl(), thisTest.getWCPath(), null, true);

        // deleted file should reapear
        thisTest.getWc().setItemTextStatus("A/B/lambda", Status.Kind.normal);

        // check the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.status functionality
     * @throws Throwable
     */
    public void testBasicStatus() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        // check the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.commit functionality
     * @throws Throwable
     */
    public void testBasicCommit() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();

        // modify file A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu",
                thisTest.getWc().getItemContent("A/mu") + "appended mu text");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu",NodeKind.file,
                CommitItemStateFlags.TextMods);

        // modify file A/D/G/rho
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("new appended text for rho");
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho",
                thisTest.getWc().getItemContent("A/D/G/rho")
                + "new appended text for rho");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho",NodeKind.file,
                CommitItemStateFlags.TextMods);

        // commit the changes
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                        true), 2);

        // check the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.update functionality
     * @throws Throwable
     */
    public void testBasicUpdate() throws Throwable
    {
        // build the test setup. Used for the changes
        OneTest thisTest = new OneTest();

        // build the backup test setup. That is the one that will be updated
        OneTest backupTest = thisTest.copy(".backup");

        // modify A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu",
                thisTest.getWc().getItemContent("A/mu") + "appended mu text");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu",NodeKind.file,
                CommitItemStateFlags.TextMods);

        // modify A/D/G/rho
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("new appended text for rho");
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho",
                thisTest.getWc().getItemContent("A/D/G/rho")
                + "new appended text for rho");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho",NodeKind.file,
                CommitItemStateFlags.TextMods);

        // commit the changes
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                        true), 2);

        // check the status of the working copy
        thisTest.checkStatus();

        // update the backup test
        assertEquals("wrong revision number from update",
                client.update(backupTest.getWCPath(), null, true), 2);

        // set the expected working copy layout for the backup test
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        backupTest.getWc().setItemContent("A/mu",
                backupTest.getWc().getItemContent("A/mu") + "appended mu text");
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        backupTest.getWc().setItemContent("A/D/G/rho",
                backupTest.getWc().getItemContent("A/D/G/rho")
                + "new appended text for rho");

        // check the status of the working copy of the backup test
        backupTest.checkStatus();
    }

    /**
     * test basic SVNClient.mkdir with url parameter functionality
     * @throws Throwable
     */
    public void testBasicMkdirUrl() throws Throwable
    {
        // build the test setup.
        OneTest thisTest = new OneTest();

        // create Y and Y/Z directories in the repository
        addExpectedCommitItem(null, thisTest.getUrl(), "Y", NodeKind.none,
                CommitItemStateFlags.Add);
        addExpectedCommitItem(null, thisTest.getUrl(), "Y/Z", NodeKind.none,
                CommitItemStateFlags.Add);
        client.mkdir(new String[]{thisTest.getUrl() + "/Y",
                                  thisTest.getUrl() + "/Y/Z"}, "log_msg");

        // add the new directories the expected working copy layout
        thisTest.getWc().addItem("Y", null);
        thisTest.getWc().setItemWorkingCopyRevision("Y", 2);
        thisTest.getWc().addItem("Y/Z", null);
        thisTest.getWc().setItemWorkingCopyRevision("Y/Z", 2);

        // update the working copy
        assertEquals("wrong revision from update",
                client.update(thisTest.getWCPath(), null, true), 2);

        // check the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.update functionality with concurrent changes
     * in the repository and the working copy
     * @throws Throwable
     */
    public void testBasicMergingUpdate() throws Throwable
    {
        // build the first working copy
        OneTest thisTest = new OneTest();

        // append 10 lines to A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        String muContent = thisTest.getWc().getItemContent("A/mu");
        for (int i = 2; i < 11; i++)
        {
            muPW.print("\nThis is line " + i + " in mu");
            muContent = muContent + "\nThis is line " + i + " in mu";
        }
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu", muContent);
        addExpectedCommitItem(thisTest.getWorkingCopy().getAbsolutePath(),
                thisTest.getUrl(), "A/mu", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // append 10 line to A/D/G/rho
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        String rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        for (int i = 2; i < 11; i++)
        {
            rhoPW.print("\nThis is line " + i + " in rho");
            rhoContent = rhoContent + "\nThis is line " + i + " in rho";
        }
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // commit the changes
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                        true), 2);

        // check the status of the first working copy
        thisTest.checkStatus();

        // create a backup copy of the working copy
        OneTest backupTest = thisTest.copy(".backup");

        // change the last line of A/mu in the first working copy
        muPW = new PrintWriter(new FileOutputStream(mu, true));
        muContent = thisTest.getWc().getItemContent("A/mu");
        muPW.print(" Appended to line 10 of mu");
        muContent = muContent + " Appended to line 10 of mu";
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 3);
        thisTest.getWc().setItemContent("A/mu", muContent);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // change the last line of A/mu in the first working copy
        rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        rhoPW.print(" Appended to line 10 of rho");
        rhoContent = rhoContent + " Appended to line 10 of rho";
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 3);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // commit these changes to the repository
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                        true), 3);

        // check the status of the first working copy
        thisTest.checkStatus();

        // modify the first line of A/mu in the backup working copy
        mu = new File(backupTest.getWorkingCopy(), "A/mu");
        muPW = new PrintWriter(new FileOutputStream(mu));
        muPW.print("This is the new line 1 in the backup copy of mu");
        muContent = "This is the new line 1 in the backup copy of mu";
        for (int i = 2; i < 11; i++)
        {
            muPW.print("\nThis is line " + i + " in mu");
            muContent = muContent + "\nThis is line " + i + " in mu";
        }
        muPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 3);
        muContent = muContent + " Appended to line 10 of mu";
        backupTest.getWc().setItemContent("A/mu", muContent);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);

        // modify the first line of A/D/G/rho in the backup working copy
        rho = new File(backupTest.getWorkingCopy(), "A/D/G/rho");
        rhoPW = new PrintWriter(new FileOutputStream(rho));
        rhoPW.print("This is the new line 1 in the backup copy of rho");
        rhoContent = "This is the new line 1 in the backup copy of rho";
        for (int i = 2; i < 11; i++)
        {
            rhoPW.print("\nThis is line " + i + " in rho");
            rhoContent = rhoContent + "\nThis is line " + i + " in rho";
        }
        rhoPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 3);
        rhoContent = rhoContent + " Appended to line 10 of rho";
        backupTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        backupTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.modified);

        // update the backup working copy
        assertEquals("wrong revision number from update",
                client.update(backupTest.getWCPath(), null, true), 3);

        // check the status of the backup working copy
        backupTest.checkStatus();
    }

    /**
     * test the basic SVNClient.update functionality with concurrent changes
     * in the repository and the working copy that generate conflicts
     * @throws Throwable
     */
    public void testBasicConflict() throws Throwable
    {
        // build the first working copy
        OneTest thisTest = new OneTest();

        // copy the first working copy to the backup working copy
        OneTest backupTest = thisTest.copy(".backup");

        // append a line to A/mu in the first working copy
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        String muContent = thisTest.getWc().getItemContent("A/mu");
        muPW.print("\nOriginal appended text for mu");
        muContent = muContent + "\nOriginal appended text for mu";
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu", muContent);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // append a line to A/D/G/rho in the first working copy
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        String rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        rhoPW.print("\nOriginal appended text for rho");
        rhoContent = rhoContent + "\nOriginal appended text for rho";
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho", NodeKind.file,
                CommitItemStateFlags.TextMods);

        // commit the changes in the first working copy
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                        true), 2);

        // test the status of the working copy after the commit
        thisTest.checkStatus();

        // append a different line to A/mu in the backup working copy
        mu = new File(backupTest.getWorkingCopy(), "A/mu");
        muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("\nConflicting appended text for mu");
        muContent = "<<<<<<< .mine\nThis is the file 'mu'.\n"+
                "Conflicting appended text for mu=======\n"+
                "This is the file 'mu'.\n"+
                "Original appended text for mu>>>>>>> .r2";
        muPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        backupTest.getWc().setItemContent("A/mu", muContent);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.conflicted);
        backupTest.getWc().addItem("A/mu.r1", "");
        backupTest.getWc().setItemNodeKind("A/mu.r1", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/mu.r1",
                Status.Kind.unversioned);
        backupTest.getWc().addItem("A/mu.r2", "");
        backupTest.getWc().setItemNodeKind("A/mu.r2", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/mu.r2",
                Status.Kind.unversioned);
        backupTest.getWc().addItem("A/mu.mine", "");
        backupTest.getWc().setItemNodeKind("A/mu.mine", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/mu.mine",
                Status.Kind.unversioned);

        // append a different line to A/D/G/rho in the backup working copy
        rho = new File(backupTest.getWorkingCopy(), "A/D/G/rho");
        rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("\nConflicting appended text for rho");
        rhoContent = "<<<<<<< .mine\nThis is the file 'rho'.\n"+
                "Conflicting appended text for rho=======\n"+
                "his is the file 'rho'.\n"+
                "Original appended text for rho>>>>>>> .r2";
        rhoPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        backupTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        backupTest.getWc().setItemTextStatus("A/D/G/rho",
                Status.Kind.conflicted);
        backupTest.getWc().addItem("A/D/G/rho.r1", "");
        backupTest.getWc().setItemNodeKind("A/D/G/rho.r1", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/D/G/rho.r1",
                Status.Kind.unversioned);
        backupTest.getWc().addItem("A/D/G/rho.r2", "");
        backupTest.getWc().setItemNodeKind("A/D/G/rho.r2", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/D/G/rho.r2",
                Status.Kind.unversioned);
        backupTest.getWc().addItem("A/D/G/rho.mine", "");
        backupTest.getWc().setItemNodeKind("A/D/G/rho.mine", NodeKind.unknown);
        backupTest.getWc().setItemTextStatus("A/D/G/rho.mine",
                Status.Kind.unversioned);

        // update the backup working copy from the repository
        assertEquals("wrong revision number from update",
                client.update(backupTest.getWCPath(), null, true), 2);

        // check the status of the backup working copy
        backupTest.checkStatus();

        // flag A/mu as resolved
        client.resolved(backupTest.getWCPath()+"/A/mu",false);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);
        backupTest.getWc().removeItem("A/mu.r1");
        backupTest.getWc().removeItem("A/mu.r2");
        backupTest.getWc().removeItem("A/mu.mine");

        // flag A/D/G/rho as resolved
        client.resolved(backupTest.getWCPath()+"/A/D/G/rho",false);
        backupTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.modified);
        backupTest.getWc().removeItem("A/D/G/rho.r1");
        backupTest.getWc().removeItem("A/D/G/rho.r2");
        backupTest.getWc().removeItem("A/D/G/rho.mine");

        // check the status after the conflicts are flaged as resolved
        backupTest.checkStatus();
    }

    /**
     * test the basic SVNClient.cleanup functionality
     * @throws Throwable
     */
    public void testBasicCleanup() throws Throwable
    {
        // create a test working copy
        OneTest thisTest = new OneTest();

        // create a lock file in A/B
        File adminLock = new File(thisTest.getWorkingCopy(),"A/B/" +
                                  getAdminDirectoryName() + "/lock");
        PrintWriter pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/B", true);

        // create a lock file in A/D/G
        adminLock = new File(thisTest.getWorkingCopy(),"A/D/G/" +
                             getAdminDirectoryName() + "/lock");
        pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/D/G", true);

        // create a lock file in A/C
        adminLock = new File(thisTest.getWorkingCopy(),"A/C/" +
                             getAdminDirectoryName() + "/lock");
        pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/C", true);

        // test the status of the working copy
        thisTest.checkStatus();

        // run cleanup
        client.cleanup(thisTest.getWCPath());
        thisTest.getWc().setItemIsLocked("A/B", false);
        thisTest.getWc().setItemIsLocked("A/D/G", false);
        thisTest.getWc().setItemIsLocked("A/C", false);

        // test the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * Test the basic SVNClient.revert functionality
      * @throws Throwable
     */
    public void testBasicRevert() throws Throwable
    {
        // create a test working copy
        OneTest thisTest = new OneTest();

        // modify A/B/E/beta
        File file = new File(thisTest.getWorkingCopy(), "A/B/E/beta");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'beta'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/B/E/beta", Status.Kind.modified);

        // modify iota
        file = new File(thisTest.getWorkingCopy(), "iota");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'iota'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("iota", Status.Kind.modified);

        // modify A/D/G/rho
        file = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'rho'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.modified);

        // create new file A/D/H/zeta and add it to subversion
        file = new File(thisTest.getWorkingCopy(), "A/D/H/zeta");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'zeta'.");
        pw.close();
        thisTest.getWc().addItem("A/D/H/zeta", "Added some text to 'zeta'.");
        thisTest.getWc().setItemTextStatus("A/D/H/zeta", Status.Kind.added);
        client.add(file.getAbsolutePath(), false);

        // test the status of the working copy
        thisTest.checkStatus();

        // revert the changes
        client.revert(thisTest.getWCPath()+"/A/B/E/beta", false);
        thisTest.getWc().setItemTextStatus("A/B/E/beta", Status.Kind.normal);
        client.revert(thisTest.getWCPath()+"/iota", false);
        thisTest.getWc().setItemTextStatus("iota", Status.Kind.normal);
        client.revert(thisTest.getWCPath()+"/A/D/G/rho", false);
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.normal);
        client.revert(thisTest.getWCPath()+"/A/D/H/zeta", false);
        thisTest.getWc().setItemTextStatus("A/D/H/zeta",
                Status.Kind.unversioned);
        thisTest.getWc().setItemNodeKind("A/D/H/zeta", NodeKind.unknown);

        // test the status of the working copy
        thisTest.checkStatus();

        // delete A/B/E/beta and revert the change
        file = new File(thisTest.getWorkingCopy(), "A/B/E/beta");
        file.delete();
        client.revert(file.getAbsolutePath(), false);

        // resurected file should not be readonly
        assertTrue("reverted file is not readonly",
                file.canWrite()&& file.canRead());

        // test the status of the working copy
        thisTest.checkStatus();

        // create & add the directory X
        client.mkdir(new String[] {thisTest.getWCPath()+"/X"}, null);
        thisTest.getWc().addItem("X", null);
        thisTest.getWc().setItemTextStatus("X", Status.Kind.added);

        // test the status of the working copy
        thisTest.checkStatus();

        // remove & revert X
        removeDirectoryWithContent(new File(thisTest.getWorkingCopy(), "X"));
        client.revert(thisTest.getWCPath()+"/X", false);
        thisTest.getWc().removeItem("X");

        // test the status of the working copy
        thisTest.checkStatus();

        // delete the directory A/B/E
        client.remove(new String[] {thisTest.getWCPath()+"/A/B/E"}, null, true);
        removeDirectoryWithContent(new File(thisTest.getWorkingCopy(), "A/B/E"));
        thisTest.getWc().setItemTextStatus("A/B/E", Status.Kind.deleted);
        thisTest.getWc().removeItem("A/B/E/alpha");
        thisTest.getWc().removeItem("A/B/E/beta");

        // test the status of the working copy
        thisTest.checkStatus();

        // revert A/B/E -> this will not resurect it
        client.revert(thisTest.getWCPath()+"/A/B/E", true);

        // test the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * thest the basic SVNClient.switch functionality
     * @throws Throwable
     */
    public void testBasicSwitch() throws Throwable
    {
        // create the test working copy
        OneTest thisTest = new OneTest();

        // switch iota to A/D/gamma
        String iotaPath = thisTest.getWCPath() + "/iota";
        String gammaUrl = thisTest.getUrl() + "/A/D/gamma";
        thisTest.getWc().setItemContent("iota",
                greekWC.getItemContent("A/D/gamma"));
        thisTest.getWc().setItemIsSwitched("iota", true);
        client.doSwitch(iotaPath, gammaUrl, null, true);

        // check the status of the working copy
        thisTest.checkStatus();

        // switch A/D/H to /A/D/G
        String adhPath = thisTest.getWCPath() + "/A/D/H";
        String adgURL = thisTest.getUrl() + "/A/D/G";
        thisTest.getWc().setItemIsSwitched("A/D/H",true);
        thisTest.getWc().removeItem("A/D/H/chi");
        thisTest.getWc().removeItem("A/D/H/omega");
        thisTest.getWc().removeItem("A/D/H/psi");
        thisTest.getWc().addItem("A/D/H/pi",
                thisTest.getWc().getItemContent("A/D/G/pi"));
        thisTest.getWc().addItem("A/D/H/rho",
                thisTest.getWc().getItemContent("A/D/G/rho"));
        thisTest.getWc().addItem("A/D/H/tau",
                thisTest.getWc().getItemContent("A/D/G/tau"));
        client.doSwitch(adhPath, adgURL, null, true);

        // check the status of the working copy
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.remove functionality
     * @throws Throwable
     */
    public void testBasicDelete() throws Throwable
    {
        // create the test working copy
        OneTest thisTest = new OneTest();

        // modify A/D/H/chi
        File file = new File(thisTest.getWorkingCopy(), "A/D/H/chi");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("added to chi");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/D/H/chi", Status.Kind.modified);

        // set a property on A/D/G/rho file
        client.propertySet(thisTest.getWCPath()+"/A/D/G/rho", "abc", "def",
                true);
        thisTest.getWc().setItemPropStatus("A/D/G/rho", Status.Kind.modified);

        // set a property on A/B/F directory
        client.propertySet(thisTest.getWCPath()+"/A/B/F", "abc", "def", false);
        thisTest.getWc().setItemPropStatus("A/B/F", Status.Kind.modified);

        // create a unversioned A/C/sigma file
        file = new File(thisTest.getWCPath(),"A/C/sigma");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("unversioned sigma");
        pw.close();
        thisTest.getWc().addItem("A/C/sigma", "unversioned sigma");
        thisTest.getWc().setItemTextStatus("A/C/sigma", Status.Kind.unversioned);
        thisTest.getWc().setItemNodeKind("A/C/sigma", NodeKind.unknown);

        // create unversioned directory A/C/Q
        file = new File(thisTest.getWCPath(), "A/C/Q");
        file.mkdir();
        thisTest.getWc().addItem("A/C/Q", null);
        thisTest.getWc().setItemNodeKind("A/C/Q", NodeKind.unknown);
        thisTest.getWc().setItemTextStatus("A/C/Q", Status.Kind.unversioned);

        // create & add the directory A/B/X
        file = new File(thisTest.getWCPath(), "A/B/X");
        client.mkdir(new String[] {file.getAbsolutePath()}, null);
        thisTest.getWc().addItem("A/B/X", null);
        thisTest.getWc().setItemTextStatus("A/B/X", Status.Kind.added);

        // create & add the file A/B/X/xi
        file = new File(file, "xi");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("added xi");
        pw.close();
        client.add(file.getAbsolutePath(),false);
        thisTest.getWc().addItem("A/B/X/xi", "added xi");
        thisTest.getWc().setItemTextStatus("A/B/X/xi", Status.Kind.added);

        // create & add the directory A/B/Y
        file = new File(thisTest.getWCPath(), "A/B/Y");
        client.mkdir(new String[] {file.getAbsolutePath()}, null);
        thisTest.getWc().addItem("A/B/Y", null);
        thisTest.getWc().setItemTextStatus("A/B/Y", Status.Kind.added);

        // test the status of the working copy
        thisTest.checkStatus();

        // the following removes should all fail without force

        try
        {
            // remove of A/D/H/chi without force should fail, because it is
            // modified
            client.remove(new String[] {thisTest.getWCPath()+"/A/D/H/chi"},
                    null, false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/D/H without force should fail, because A/D/H/chi is
            // modified
            client.remove(new String[] {thisTest.getWCPath()+"/A/D/H"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/D/G/rho without force should fail, because it has
            // a new property
            client.remove(new String[] {thisTest.getWCPath()+"/A/D/G/rho"},
                    null, false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/D/G without force should fail, because A/D/G/rho has
            // a new property
            client.remove(new String[] {thisTest.getWCPath()+"/A/D/G"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/B/F without force should fail, because it has
            // a new property
            client.remove(new String[] {thisTest.getWCPath()+"/A/B/F"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/B without force should fail, because A/B/F has
            // a new property
            client.remove(new String[] {thisTest.getWCPath()+"/A/B"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/C/sigma without force should fail, because it is
            // unversioned
            client.remove(new String[] {thisTest.getWCPath()+"/A/C/sigma"},
                    null, false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/C without force should fail, because A/C/sigma is
            // unversioned
            client.remove(new String[] {thisTest.getWCPath()+"/A/C"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        try
        {
            // remove of A/B/X without force should fail, because it is new
            client.remove(new String[] {thisTest.getWCPath()+"/A/B/X"}, null,
                    false);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        // check the status of the working copy
        thisTest.checkStatus();

        // the following removes should all work
        client.remove(new String[] {thisTest.getWCPath()+"/A/B/E"}, null,
                false);
        thisTest.getWc().setItemTextStatus("A/B/E",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/E/alpha",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/E/beta",Status.Kind.deleted);
        client.remove(new String[] {thisTest.getWCPath()+"/A/D/H"}, null, true);
        thisTest.getWc().setItemTextStatus("A/D/H",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/chi",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/omega",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/psi",Status.Kind.deleted);
        client.remove(new String[] {thisTest.getWCPath()+"/A/D/G"}, null, true);
        thisTest.getWc().setItemTextStatus("A/D/G",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/rho",Status.Kind.deleted);
        thisTest.getWc().setItemPropStatus("A/D/G/rho", Status.Kind.none);
        thisTest.getWc().setItemTextStatus("A/D/G/pi",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/tau",Status.Kind.deleted);
        client.remove(new String[] {thisTest.getWCPath()+"/A/B/F"}, null, true);
        thisTest.getWc().setItemTextStatus("A/B/F",Status.Kind.deleted);
        thisTest.getWc().setItemPropStatus("A/B/F", Status.Kind.none);
        client.remove(new String[] {thisTest.getWCPath()+"/A/C"}, null, true);
        thisTest.getWc().setItemTextStatus("A/C",Status.Kind.deleted);
        client.remove(new String[] {thisTest.getWCPath()+"/A/B/X"}, null, true);
        file = new File(thisTest.getWorkingCopy(), "iota");
        file.delete();
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        thisTest.getWc().setItemTextStatus("iota",Status.Kind.deleted);
        file = new File(thisTest.getWorkingCopy(), "A/D/gamma");
        file.delete();
        client.remove(new String[] {file.getAbsolutePath()}, null, false);
        thisTest.getWc().setItemTextStatus("A/D/gamma",Status.Kind.deleted);
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        client.remove(new String[] {thisTest.getWCPath()+"/A/B/E"}, null,
                false);
        thisTest.getWc().removeItem("A/B/X");
        thisTest.getWc().removeItem("A/B/X/xi");
        thisTest.getWc().removeItem("A/C/sigma");
        thisTest.getWc().removeItem("A/C/Q");
        thisTest.checkStatus();
        client.remove(new String[] {thisTest.getWCPath()+"/A/D"},null, true);
        thisTest.getWc().setItemTextStatus("A/D", Status.Kind.deleted);
        thisTest.getWc().removeItem("A/D/Y");

        // check the status of the working copy
        thisTest.checkStatus();

        // confirm that the file are realy deleted
        assertFalse("failed to remove text modified file",
                new File(thisTest.getWorkingCopy(), "A/D/G/rho").exists());
        assertFalse("failed to remove prop modified file",
                new File(thisTest.getWorkingCopy(), "A/D/H/chi").exists());
        assertFalse("failed to remove unversioned file",
                new File(thisTest.getWorkingCopy(), "A/C/sigma").exists());
        assertFalse("failed to remove unmodified file",
                new File(thisTest.getWorkingCopy(), "A/B/E/alpha").exists());
        file = new File(thisTest.getWorkingCopy(),"A/B/F");
        assertTrue("removed versioned dir", file.exists()
                && file.isDirectory());
        assertFalse("failed to remove unversioned dir",
                new File(thisTest.getWorkingCopy(), "A/C/Q").exists());
        assertFalse("failed to remove added dir",
                new File(thisTest.getWorkingCopy(), "A/B/X").exists());

        // delete unversioned file foo
        file = new File(thisTest.getWCPath(),"foo");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("unversioned foo");
        pw.close();
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        assertFalse("failed to remove unversioned file foo", file.exists());

        try
        {
            // delete non-existant file foo
            client.remove(new String[] {file.getAbsolutePath()}, null, true);
            fail("missing exception");
        }
        catch(ClientException expected)
        {
        }

        // delete file iota in the repository
        addExpectedCommitItem(null, thisTest.getUrl(), "iota", NodeKind.none,
                CommitItemStateFlags.Delete);
        client.remove(new String[] {thisTest.getUrl()+"/iota"},
                "delete iota URL", false);
    }

    public void testBasicCheckoutDeleted() throws Throwable
    {
        // create working copy
        OneTest thisTest = new OneTest();

        // delete A/D and its content
        client.remove(new String[] {thisTest.getWCPath()+"/A/D"}, null, true);
        thisTest.getWc().setItemTextStatus("A/D", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/pi", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/tau", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/chi", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/psi", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/omega", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/gamma", Status.Kind.deleted);

        // check the working copy status
        thisTest.checkStatus();

        // commit the change
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D", NodeKind.dir,
                CommitItemStateFlags.Delete);
        assertEquals("wrong revision from commit",
                client.commit(new String[]{thisTest.getWCPath()}, "log message",
                        true),2);
        thisTest.getWc().removeItem("A/D");
        thisTest.getWc().removeItem("A/D/G");
        thisTest.getWc().removeItem("A/D/G/rho");
        thisTest.getWc().removeItem("A/D/G/pi");
        thisTest.getWc().removeItem("A/D/G/tau");
        thisTest.getWc().removeItem("A/D/H");
        thisTest.getWc().removeItem("A/D/H/chi");
        thisTest.getWc().removeItem("A/D/H/psi");
        thisTest.getWc().removeItem("A/D/H/omega");
        thisTest.getWc().removeItem("A/D/gamma");

        // check the working copy status
        thisTest.checkStatus();

        // check out the previous revision
        client.checkout(thisTest.getUrl()+"/A/D", thisTest.getWCPath()+"/new_D",
                new Revision.Number(1), true);
    }

    /**
     * Test if Subversion will detect the change of a file to a direcory
     * @throws Throwable
     */
    public void testBasicNodeKindChange() throws Throwable
    {
        // create working copy
        OneTest thisTest = new OneTest();

        //  remove A/D/gamma
        client.remove(new String[] {thisTest.getWCPath()+"/A/D/gamma"}, null,
                false);
        thisTest.getWc().setItemTextStatus("A/D/gamma", Status.Kind.deleted);

        // check the working copy status
        thisTest.checkStatus();

        try
        {
            // creating a directory in the place of the deleted file should
            // fail
            client.mkdir(new String[] {thisTest.getWCPath()+"/A/D/gamma"},
                    null);
            fail("can change node kind");
        }
        catch(ClientException e)
        {

        }

        // check the working copy status
        thisTest.checkStatus();

        // commit the deletion
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/gamma", NodeKind.file,
                CommitItemStateFlags.Delete);
        assertEquals("wrong revision number from commit",
                client.commit(new String[]{thisTest.getWCPath()},"log message",
                        true), 2);
        thisTest.getWc().removeItem("A/D/gamma");

        // check the working copy status
        thisTest.checkStatus();

        try
        {
            // creating a directory in the place of the deleted file should
            // still fail
            client.mkdir(
                    new String[] {thisTest.getWCPath()+"/A/D/gamma"}, null);
            fail("can change node kind");
        }
        catch(ClientException e)
        {

        }

        // check the working copy status
        thisTest.checkStatus();

        // update the working copy
        client.update(thisTest.getWCPath(), null, true);

        // check the working copy status
        thisTest.checkStatus();

        // now creating the directory should succeed
        client.mkdir(new String[] {thisTest.getWCPath()+"/A/D/gamma"}, null);
        thisTest.getWc().addItem("A/D/gamma", null);
        thisTest.getWc().setItemTextStatus("A/D/gamma", Status.Kind.added);

        // check the working copy status
        thisTest.checkStatus();
    }

    /**
     * Test the basic SVNClient.import functionality
     * @throws Throwable
     */
    public void testBasicImport() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // create new_file
        File file = new File(thisTest.getWCPath(),"new_file");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file));
        pw.print("some text");
        pw.close();

        // import new_file info dirA/dirB/newFile
        addExpectedCommitItem(thisTest.getWCPath(),
                null, "new_file", NodeKind.none, CommitItemStateFlags.Add);
        client.doImport(file.getAbsolutePath(),
                thisTest.getUrl()+"/dirA/dirB/new_file",
                "log message for new import", true);

        // delete new_file
        file.delete();

        // update the working
        assertEquals("wrong revision from update",
                client.update(thisTest.getWCPath(), null, true),2);
        thisTest.getWc().addItem("dirA", null);
        thisTest.getWc().setItemWorkingCopyRevision("dirA",2);
        thisTest.getWc().addItem("dirA/dirB", null);
        thisTest.getWc().setItemWorkingCopyRevision("dirA/dirB",2);
        thisTest.getWc().addItem("dirA/dirB/new_file", "some text");
        thisTest.getWc().setItemWorkingCopyRevision("dirA/dirB/new_file",2);

        // test the working copy status
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.fileContent functionality
     * @throws Throwable
     */
    public void testBasicCat() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // modify A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter pw = new PrintWriter(new FileOutputStream(mu, true));
        pw.print("some text");
        pw.close();
        // get the content from the repository
        byte[] content = client.fileContent(thisTest.getWCPath()+"/A/mu", null);
        byte[] testContent = thisTest.getWc().getItemContent("A/mu").getBytes();

        // the content should be the same
        assertTrue("content changed", Arrays.equals(content, testContent));
    }

    /**
     * test the basic SVNClient.fileContent functionality
     * @throws Throwable
     */
    public void testBasicCatStream() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // modify A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter pw = new PrintWriter(new FileOutputStream(mu, true));
        pw.print("some text");
        pw.close();
        // get the content from the repository
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        client.streamFileContent(thisTest.getWCPath() + "/A/mu", null, null,
                                 100, baos);
        
        byte[] content = baos.toByteArray();
        byte[] testContent = thisTest.getWc().getItemContent("A/mu").getBytes();

        // the content should be the same
        assertTrue("content changed", Arrays.equals(content, testContent));
    }

    /**
     * test the basic SVNClient.list functionality
     * @throws Throwable
     */
    public void testBasicLs() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // list the repository root dir
        DirEntry[] entries = client.list(thisTest.getWCPath(), null, false);
        thisTest.getWc().check(entries,"", false);

        // list directory A
        entries = client.list(thisTest.getWCPath()+"/A", null, false);
        thisTest.getWc().check(entries,"A", false);

        // list directory A in BASE revision
        entries = client.list(thisTest.getWCPath()+"/A", Revision.BASE, false);
        thisTest.getWc().check(entries,"A", false);

        // list file A/mu
        entries = client.list(thisTest.getWCPath()+"/A/mu", null, false);
        thisTest.getWc().check(entries,"A/mu");
    }

    /**
     * test the basis SVNClient.add functionality with files that should be
     * ignored
     * @throws Throwable
     */
    public void testBasicAddIgnores() throws Throwable
    {
        // create working copy
        OneTest thisTest = new OneTest();

        // create dir
        File dir = new File(thisTest.getWorkingCopy(), "dir");
        dir.mkdir();

        // create dir/foo.c
        File fileC = new File(dir, "foo.c");
        new FileOutputStream(fileC).close();

        // create dir/foo.o (should be ignored)
        File fileO = new File(dir, "foo.o");
        new FileOutputStream(fileO).close();

        // add dir
        client.add(dir.getAbsolutePath(), true);
        thisTest.getWc().addItem("dir", null);
        thisTest.getWc().setItemTextStatus("dir",Status.Kind.added);
        thisTest.getWc().addItem("dir/foo.c", "");
        thisTest.getWc().setItemTextStatus("dir/foo.c",Status.Kind.added);
        thisTest.getWc().addItem("dir/foo.o", "");
        thisTest.getWc().setItemTextStatus("dir/foo.o",Status.Kind.ignored);
        thisTest.getWc().setItemNodeKind("dir/foo.o", NodeKind.unknown);

        // test the working copy status
        thisTest.checkStatus();
    }

    /**
     * test the basis SVNClient.import functionality with files that should be
     * ignored
     * @throws Throwable
     */
    public void testBasicImportIgnores() throws Throwable
    {
        // create working copy
        OneTest thisTest = new OneTest();

        // create dir
        File dir = new File(thisTest.getWorkingCopy(), "dir");
        dir.mkdir();

        // create dir/foo.c
        File fileC = new File(dir, "foo.c");
        new FileOutputStream(fileC).close();

        // create dir/foo.o (should be ignored)
        File fileO = new File(dir, "foo.o");
        new FileOutputStream(fileO).close();

        // import dir
        addExpectedCommitItem(thisTest.getWCPath(),
                null, "dir", NodeKind.none, CommitItemStateFlags.Add);
        client.doImport(dir.getAbsolutePath(), thisTest.getUrl()+"/dir",
                "log message for import", true);

        // remove dir
        removeDirectoryWithContent(dir);

        // udpate the working copy
        assertEquals("wrong revision from update", 2,
                client.update(thisTest.getWCPath(), null, true));
        thisTest.getWc().addItem("dir", null);
        thisTest.getWc().addItem("dir/foo.c", "");

        // test the working copy status
        thisTest.checkStatus();
    }

    /**
     * test the basic SVNClient.info functionality
     * @throws Throwable
     */
    public void testBasicInfo() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // get the item information and test it
        Info info = client.info(thisTest.getWCPath()+"/A/mu");
        assertEquals("wrong revision from info", 1,
                info.getLastChangedRevision());
        assertEquals("wrong schedule kind from info", ScheduleKind.normal,
                info.getSchedule());
        assertEquals("wrong node kind from info", NodeKind.file,
                info.getNodeKind());
    }

    /**
     * test the basic SVNClient.logMessage functionality
     * @throws Throwable
     */
    public void testBasicLogMessage() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();

        // get the commit message of the initial import and test it
        LogMessage lm[] = client.logMessages(thisTest.getWCPath(), null,
                null, false, true);
        assertEquals("wrong number of objects", 1, lm.length);
        assertEquals("wrong message", "Log Message", lm[0].getMessage());
        assertEquals("wrong revision", 1, lm[0].getRevisionNumber());
        assertEquals("wrong user", "jrandom", lm[0].getAuthor());
        assertNotNull("changed paths set", lm[0].getChangedPaths());
        ChangePath cp[] = lm[0].getChangedPaths();
        assertEquals("wrong number of chang pathes", 20, cp.length);
        assertEquals("wrong path", "/A", cp[0].getPath());
        assertEquals("wrong copy source rev", -1, cp[0].getCopySrcRevision());
        assertNull("wrong copy source path", cp[0].getCopySrcPath());
        assertEquals("wrong action", 'A', cp[0].getAction());
    }

    /**
     * test the basic SVNClient.getVersionInfo functionality
     * @throws Throwable
     * @since 1.2
     */
    public void testBasicVersionInfo() throws Throwable
    {
        // create the working copy
        OneTest thisTest = new OneTest();
        assertEquals("wrong version info","1",
                client.getVersionInfo(thisTest.getWCPath(), null, false));        
    }

    /**
     * test the baisc SVNClient locking functionality
     * @throws Throwable
     * @since 1.2
     */
    public void testBasicLocking() throws Throwable
    {
        // build the first working copy
        OneTest thisTest = new OneTest();

        client.propertySet(thisTest.getWCPath()+"/A/mu",
                           PropertyData.NEEDS_LOCK, "*", false);

        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu",NodeKind.file,
                CommitItemStateFlags.PropMods);
        assertEquals("bad revision number on commit", 2,
                     client.commit(new String[] {thisTest.getWCPath()},
                                   "message", true));
        File f = new File(thisTest.getWCPath()+"/A/mu");
        assertEquals("file should be read only now", false, f.canWrite());
        client.lock(new String[] {thisTest.getWCPath()+"/A/mu"},
                                "comment", false);
        assertEquals("file should be read write now", true, f.canWrite());
        client.unlock(new String[]{thisTest.getWCPath()+"/A/mu"},
                false);
        assertEquals("file should be read only now", false, f.canWrite());
        client.lock(new String[]{thisTest.getWCPath()+"/A/mu"},
                           "comment", false);
        assertEquals("file should be read write now", true, f.canWrite());
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu",NodeKind.file,
                    0);
        assertEquals("rev number from commit",-1, client.commit(
                new String[]{thisTest.getWCPath()},"message", true));
        assertEquals("file should be read write now", true, f.canWrite());

        try
        {
            // Attempt to lock an invalid path
            client.lock(new String[]{thisTest.getWCPath()+"/A/mu2"}, "comment",
                        false);
            fail("missing exception");
        }
        catch (ClientException expected)
        {
        }
    }

    /**
     * test the baisc SVNClient.info2 functionality 
     * @throws Throwable
     * @since 1.2
     */
    public void testBasicInfo2() throws Throwable
    {
        // build the first working copy
        OneTest thisTest = new OneTest();

        Info2[] infos = client.info2(thisTest.getWCPath(), null, null, false);
        assertEquals("this should return 1 info object", 1, infos.length);
        infos = client.info2(thisTest.getWCPath(), null, null, true);
        assertEquals("this should return 21 info objects", 21, infos.length);
        infos = client.info2(thisTest.getWCPath(), new Revision.Number(1),
                             new Revision.Number(1), true);
        assertEquals("this should return 21 info objects", 21, infos.length);
    }

    /**
     * test the basic SVNClient.merge functionality
     * @throws Throwable
     * @since 1.2
     */
    public void testBasicMerge() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();
        
        // create branches directory in the repository
        addExpectedCommitItem(null, thisTest.getUrl(), "branches", NodeKind.none,
              CommitItemStateFlags.Add);
        client.mkdir(new String[]{thisTest.getUrl() + "/branches"}, "log_msg");

        // copy A to branches
        addExpectedCommitItem(null, thisTest.getUrl(), "branches/A", NodeKind.none,
                CommitItemStateFlags.Add);
        client.copy(thisTest.getUrl() + "/A", thisTest.getUrl() + "/branches/A", "create A branch", Revision.HEAD);

        // update the WC so that it has the branches folder
        client.update(thisTest.getWCPath(), Revision.HEAD, true);

        // modify file A/mu
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 4);
        thisTest.getWc().setItemContent("A/mu",
                thisTest.getWc().getItemContent("A/mu") + "appended mu text");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/mu",NodeKind.file,
                CommitItemStateFlags.TextMods);

        // modify file A/D/G/rho
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("new appended text for rho");
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 4);
        thisTest.getWc().setItemContent("A/D/G/rho",
                thisTest.getWc().getItemContent("A/D/G/rho")
                + "new appended text for rho");
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "A/D/G/rho",NodeKind.file,
                CommitItemStateFlags.TextMods);
        // commit the changes
        assertEquals("wrong revision number from commit",
              client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                      true), 4);

        // merge changes in A to branches/A
        String branchPath = thisTest.getWCPath() + "/branches/A";
        String modUrl = thisTest.getUrl() + "/A";
        // test --dry-run
        client.merge(modUrl, new Revision.Number(2), modUrl, Revision.HEAD, branchPath, false, true, false, true);

        // now do the real merge
        client.merge(modUrl, new Revision.Number(2), modUrl, Revision.HEAD, branchPath, false, true, false, false);
        
        // commit the changes so that we can verify merge
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "branches/A/mu",NodeKind.file,
                CommitItemStateFlags.TextMods);
        addExpectedCommitItem(thisTest.getWCPath(),
                thisTest.getUrl(), "branches/A/D/G/rho",NodeKind.file,
                CommitItemStateFlags.TextMods);
        assertEquals("wrong revision number from commit",
              client.commit(new String[]{thisTest.getWCPath()}, "log msg",
                      true), 5);
 
    }
    /**
     * test the basic SVNClient.isAdminDirectory functionality
     * @throws Throwable
     * @since 1.2
     */
    public void testBasicIsAdminDirectory() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();
        Notify2 notify = new Notify2(){
            public void onNotify(NotifyInformation info)
            {
                client.isAdminDirectory(".svn");
            }
        };
        client.notification2(notify);
        // update the test
        assertEquals("wrong revision number from update",
                client.update(thisTest.getWCPath(), null, true), 1);
    }
    public void testBasicCancelOperation() throws Throwable
    {
        // build the test setup
        OneTest thisTest = new OneTest();
        Notify2 notify = new Notify2(){
            public void onNotify(NotifyInformation info)
            {
                try
                {
                    client.cancelOperation();
                }
                catch (ClientException e)
                {
                    fail(e.getMessage());
                }
            }
        };
        client.notification2(notify);
        // update the test to try to cancel an operation
        try
        {
            client.update(thisTest.getWCPath(), null, true);
            fail("missing exception for canceled operation");
        }
        catch (ClientException e)
        {
            // this is expected
        }
    }
}
