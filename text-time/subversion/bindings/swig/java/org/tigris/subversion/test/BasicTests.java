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
import java.io.PrintWriter;
import java.util.Arrays;

import junit.framework.Test;
import junit.framework.TestSuite;

import org.tigris.subversion.util.NativeResources;

public class BasicTests extends SVNTests
{
    static
    {
        testCounter = 0;
    }

    /**
     * @param name Method name to invoke.
     */
    public BasicTests(String name)
    {
        super(name);
    }

    public static Test suite()
    {
        return new TestSuite(BasicTests.class);
    }

    public static void main(String[] args)
    {
        junit.textui.TestRunner.run(suite());
    }

    public void testInitialization()
    {
        System.out.println("Initializing SVN native resources...");
        NativeResources.getInstance().initialize();
        System.out.println("Success!");
    }

    /*
    public void testBasicCheckout() throws Throwable
    {
        OneTest thisTest = new OneTest();
        try
        {
            client.checkout(thisTest.getURL() + "/A",
                            thisTest.getWorkingCopy().getAbsolutePath(),
                            null, true);
            fail("missing exception");
        }
        catch (ClientException e)
        {
        }
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);
        File lambda = new File(thisTest.getWorkingCopy(), "A/B/lambda");
        lambda.delete();
        thisTest.getWc().setItemTextStatus("A/B/lambda", Status.Kind.missing);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()
                                    + "/A/D/G"}, null, false);
        thisTest.getWc().setItemTextStatus("A/D/G", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/pi", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/tau", Status.Kind.deleted);
        thisTest.checkStatus();
        client.checkout(thisTest.getURL(),
                        thisTest.getWorkingCopy().getAbsolutePath(),
                        null, true);
        thisTest.getWc().setItemTextStatus("A/B/lambda", Status.Kind.normal);
        thisTest.checkStatus();
    }

    public void testBasicStatus() throws Throwable
    {
        OneTest thisTest = new OneTest();
        thisTest.checkStatus();
    }

    public void testBasicCommit() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu",
                                        thisTest.getWc().getItemContent("A/mu")
                                        + "appended mu text");
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("new appended text for rho");
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", thisTest.getWc().getItemContent("A/D/G/rho") + "new appended text for rho");
        assertEquals("wrong revision number from commit", client.commit(new String[]{thisTest.getWorkingCopy().getAbsolutePath()}, "log msg", true), 2);
        thisTest.checkStatus();
    }

    public void testBasicUpdate() throws Throwable
    {
        OneTest thisTest = new OneTest();
        OneTest backupTest = thisTest.copy(".backup");
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        muPW.print("appended mu text");
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu",
                                        thisTest.getWc().getItemContent("A/mu")
                                        + "appended mu text");
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoPW.print("new appended text for rho");
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", thisTest.getWc().getItemContent("A/D/G/rho") + "new appended text for rho");
        assertEquals("wrong revision number from commit",
                     client.commit(new String[] {thisTest.getWorkingCopy().getAbsolutePath()},
                     "log msg", true), 2);
        thisTest.checkStatus();
        assertEquals("wrong revision number from update",
                     client.update(backupTest.getWorkingCopy().getAbsolutePath(),
                                   null, true), 2);
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        backupTest.getWc().setItemContent("A/mu",
                                          backupTest.getWc().getItemContent("A/mu") + "appended mu text");
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        backupTest.getWc().setItemContent("A/D/G/rho",
                                          backupTest.getWc().getItemContent("A/D/G/rho") + "new appended text for rho");
        backupTest.checkStatus();
    }

    public void testBasicMkdirUrl() throws Throwable
    {
        OneTest thisTest = new OneTest();
        String url = thisTest.getURL();
        client.mkdir(new String[] { url + "/Y", url + "/Y/Z" }, "log_msg");
        thisTest.getWc().addItem("Y", null);
        thisTest.getWc().setItemWorkingCopyRevision("Y", 2);
        thisTest.getWc().addItem("Y/Z", null);
        thisTest.getWc().setItemWorkingCopyRevision("Y/Z", 2);
        assertEquals("wrong revision from update",
                     client.update(thisTest.getWorkingCopy().getAbsolutePath(),
                                   null, true), 2);
        thisTest.checkStatus();
    }

    public void testBasicMergingUpdate() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        String muContent = thisTest.getWc().getItemContent("A/mu");
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        String rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        for (int i = 2; i < 11; i++)
        {
            muPW.print("\nThis is line " + i + " in mu");
            muContent = muContent + "\nThis is line " + i + " in mu";
            rhoPW.print("\nThis is line " + i + " in rho");
            rhoContent = rhoContent + "\nThis is line " + i + " in rho";
        }
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu", muContent);
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        assertEquals("wrong revision number from commit",
                     client.commit(new String[] { thisTest.getWorkingCopy().getAbsolutePath() },
                                   "log msg", true), 2);
        thisTest.checkStatus();
        OneTest backupTest = thisTest.copy(".backup");
        muPW = new PrintWriter(new FileOutputStream(mu, true));
        muContent = thisTest.getWc().getItemContent("A/mu");
        rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        muPW.print(" Appended to line 10 of mu");
        muContent = muContent + " Appended to line 10 of mu";
        rhoPW.print(" Appended to line 10 of rho");
        rhoContent = rhoContent + " Appended to line 10 of rho";
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 3);
        thisTest.getWc().setItemContent("A/mu", muContent);
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 3);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        assertEquals("wrong revision number from commit",
                     client.commit(new String[] { thisTest.getWorkingCopy().getAbsolutePath() },
                                   "log msg", true), 3);
        thisTest.checkStatus();
        mu = new File(backupTest.getWorkingCopy(), "A/mu");
        muPW = new PrintWriter(new FileOutputStream(mu));
        muPW.print("This is the new line 1 in the backup copy of mu");
        muContent = "This is the new line 1 in the backup copy of mu";
        rho = new File(backupTest.getWorkingCopy(), "A/D/G/rho");
        rhoPW = new PrintWriter(new FileOutputStream(rho));
        rhoPW.print("This is the new line 1 in the backup copy of rho");
        rhoContent = "This is the new line 1 in the backup copy of rho";
        for (int i = 2; i < 11; i++)
        {
            muPW.print("\nThis is line " + i + " in mu");
            muContent = muContent + "\nThis is line " + i + " in mu";
            rhoPW.print("\nThis is line " + i + " in rho");
            rhoContent = rhoContent + "\nThis is line " + i + " in rho";
        }
        muPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 3);
        muContent = muContent + " Appended to line 10 of mu";
        backupTest.getWc().setItemContent("A/mu", muContent);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);
        rhoPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 3);
        rhoContent = rhoContent + " Appended to line 10 of rho";
        backupTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        backupTest.getWc().setItemTextStatus("A/D/G/rho",
                                             Status.Kind.modified);
        assertEquals("wrong revision number from update",
                     client.update(backupTest.getWorkingCopy().getAbsolutePath(),
                                   null, true), 3);
        backupTest.checkStatus();
    }

    public void testBasicConflict() throws Throwable
    {
        OneTest thisTest = new OneTest();
        OneTest backupTest = thisTest.copy(".backup");
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter muPW = new PrintWriter(new FileOutputStream(mu, true));
        String muContent = thisTest.getWc().getItemContent("A/mu");
        File rho = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        PrintWriter rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        String rhoContent = thisTest.getWc().getItemContent("A/D/G/rho");
        muPW.print("\nOriginal appended text for mu");
        muContent = muContent + "\nOriginal appended text for mu";
        rhoPW.print("\nOriginal appended text for rho");
        rhoContent = rhoContent + "\nOriginal appended text for rho";
        muPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        thisTest.getWc().setItemContent("A/mu", muContent);
        rhoPW.close();
        thisTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        thisTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        assertEquals("wrong revision number from commit", client.commit(new String[]{thisTest.getWorkingCopy().getAbsolutePath()}, "log msg", true), 2);
        thisTest.checkStatus();
        mu = new File(backupTest.getWorkingCopy(), "A/mu");
        muPW = new PrintWriter(new FileOutputStream(mu, true));
        rho = new File(backupTest.getWorkingCopy(), "A/D/G/rho");
        rhoPW = new PrintWriter(new FileOutputStream(rho, true));
        muPW.print("\nConflicting appended text for mu");
        muContent = "<<<<<<< .mine\nThis is the file 'mu'.\nConflicting appended text for mu=======\nThis is the file 'mu'.\nOriginal appended text for mu>>>>>>> .r2";
        rhoPW.print("\nConflicting appended text for rho");
        rhoContent = "<<<<<<< .mine\nThis is the file 'rho'.\nConflicting appended text for rho=======\nhis is the file 'rho'.\nOriginal appended text for rho>>>>>>> .r2";
        muPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/mu", 2);
        backupTest.getWc().setItemContent("A/mu", muContent);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.conflicted);
        rhoPW.close();
        backupTest.getWc().setItemWorkingCopyRevision("A/D/G/rho", 2);
        backupTest.getWc().setItemContent("A/D/G/rho", rhoContent);
        backupTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.conflicted);
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
        assertEquals("wrong revision number from update",
                     client.update(backupTest.getWorkingCopy().getAbsolutePath(),
                                   null, true), 2);
        backupTest.checkStatus();
        client.resolved(backupTest.getWorkingCopy().getAbsolutePath() +
                       "/A/mu", false);
        client.resolved(backupTest.getWorkingCopy().getAbsolutePath() +
                        "/A/D/G/rho", false);
        backupTest.getWc().setItemTextStatus("A/mu", Status.Kind.modified);
        backupTest.getWc().removeItem("A/mu.r1");
        backupTest.getWc().removeItem("A/mu.r2");
        backupTest.getWc().removeItem("A/mu.mine");
        backupTest.getWc().setItemTextStatus("A/D/G/rho",
                                             Status.Kind.modified);
        backupTest.getWc().removeItem("A/D/G/rho.r1");
        backupTest.getWc().removeItem("A/D/G/rho.r2");
        backupTest.getWc().removeItem("A/D/G/rho.mine");
        backupTest.checkStatus();
    }

    public void testBasicCleanup() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File adminLock = new File(thisTest.getWorkingCopy(),"A/B/.svn/lock");
        PrintWriter pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/B", true);
        adminLock = new File(thisTest.getWorkingCopy(),"A/D/G/.svn/lock");
        pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/D/G", true);
        adminLock = new File(thisTest.getWorkingCopy(),"A/C/.svn/lock");
        pw = new PrintWriter(new FileOutputStream(adminLock));
        pw.print("stop looking!");
        pw.close();
        thisTest.getWc().setItemIsLocked("A/C", true);
        thisTest.checkStatus();
        client.cleanup(thisTest.getWorkingCopy().getAbsolutePath());
        thisTest.getWc().setItemIsLocked("A/B", false);
        thisTest.getWc().setItemIsLocked("A/D/G", false);
        thisTest.getWc().setItemIsLocked("A/C", false);
        thisTest.checkStatus();
    }

    public void testBasicRevert() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File file = new File(thisTest.getWorkingCopy(), "A/B/E/beta");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'beta'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/B/E/beta", Status.Kind.modified);
        file = new File(thisTest.getWorkingCopy(), "iota");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'iota'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("iota", Status.Kind.modified);
        file = new File(thisTest.getWorkingCopy(), "A/D/G/rho");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'rho'.");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.modified);
        file = new File(thisTest.getWorkingCopy(), "A/D/H/zeta");
        pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("Added some text to 'zeta'.");
        pw.close();
        thisTest.getWc().addItem("A/D/H/zeta", "Added some text to 'zeta'.");
        thisTest.getWc().setItemTextStatus("A/D/H/zeta", Status.Kind.added);
        client.add(file.getAbsolutePath(), false);
        thisTest.checkStatus();
        client.revert(thisTest.getWorkingCopy().getAbsolutePath() +
                      "/A/B/E/beta", false);
        client.revert(thisTest.getWorkingCopy().getAbsolutePath() + "/iota",
                      false);
        client.revert(thisTest.getWorkingCopy().getAbsolutePath() +
                      "/A/D/G/rho", false);
        client.revert(thisTest.getWorkingCopy().getAbsolutePath() +
                      "/A/D/H/zeta", false);
        thisTest.getWc().setItemTextStatus("A/B/E/beta", Status.Kind.normal);
        thisTest.getWc().setItemTextStatus("iota", Status.Kind.normal);
        thisTest.getWc().setItemTextStatus("A/D/G/rho", Status.Kind.normal);
        thisTest.getWc().setItemTextStatus("A/D/H/zeta",
                                           Status.Kind.unversioned);
        thisTest.getWc().setItemNodeKind("A/D/H/zeta", NodeKind.unknown);
        thisTest.checkStatus();

        file = new File(thisTest.getWorkingCopy(), "A/B/E/beta");
        file.delete();
        client.revert(file.getAbsolutePath(), false);
        assertTrue("reverted file is not readonly",
                   file.canWrite() && file.canRead());
        thisTest.checkStatus();

        client.mkdir(new String[] {thisTest.getWorkingCopy().getAbsolutePath()
                     + "/X"}, null);
        thisTest.getWc().addItem("X", null);
        thisTest.getWc().setItemTextStatus("X", Status.Kind.added);
        thisTest.checkStatus();

        removeDirectoryWithContent(new File(thisTest.getWorkingCopy(), "X"));
        client.revert(thisTest.getWorkingCopy().getAbsolutePath()+"/X", false);
        thisTest.getWc().removeItem("X");
        thisTest.checkStatus();

        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()
                                    + "/A/B/E"}, null, true);
        removeDirectoryWithContent(new File(thisTest.getWorkingCopy(),
                                            "A/B/E"));
        thisTest.getWc().setItemTextStatus("A/B/E", Status.Kind.deleted);
        thisTest.getWc().removeItem("A/B/E/alpha");
        thisTest.getWc().removeItem("A/B/E/beta");
        thisTest.checkStatus();
        client.revert(thisTest.getWorkingCopy().getAbsolutePath() + "/A/B/E",
                      true);
        thisTest.checkStatus();
    }

    public void testBasicSwitch() throws Throwable
    {
        OneTest thisTest = new OneTest();
        String iotaPath = thisTest.getWorkingCopy().getAbsolutePath() +
            "\\iota";
        String gammaUrl = thisTest.getURL() + "/A/D/gamma";
        thisTest.getWc().setItemContent("iota",
                                        greekWC.getItemContent("A/D/gamma"));
        thisTest.getWc().setItemIsSwitched("iota", true);
        client.doSwitch(iotaPath, gammaUrl, null, true);
        thisTest.checkStatus();

        String adhPath = thisTest.getWorkingCopy().getAbsolutePath() +
            "/A/D/H";
        String adgURL = thisTest.getURL() + "/A/D/G";
        thisTest.getWc().setItemIsSwitched("A/D/H",true);
        thisTest.getWc().removeItem("A/D/H/chi");
        thisTest.getWc().removeItem("A/D/H/omega");
        thisTest.getWc().removeItem("A/D/H/psi");
        thisTest.getWc().addItem("A/D/H/pi",
                                 thisTest.getWc().getItemContent("A/D/G/pi"));
        thisTest.getWc().addItem("A/D/H/rho", thisTest.getWc().getItemContent("A/D/G/rho"));
        thisTest.getWc().addItem("A/D/H/tau", thisTest.getWc().getItemContent("A/D/G/tau"));
        client.doSwitch(adhPath, adgURL, null, true);
        thisTest.checkStatus();
    }

    public void testBasicDelete() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File file = new File(thisTest.getWorkingCopy(), "A/D/H/chi");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file, true));
        pw.print("added to chi");
        pw.close();
        thisTest.getWc().setItemTextStatus("A/D/H/chi", Status.Kind.modified);
        client.propertySet(thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/G/rho", "abc", "def", true);
        thisTest.getWc().setItemPropStatus("A/D/G/rho", Status.Kind.modified);
        client.propertySet(thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/F", "abc", "def", false);
        thisTest.getWc().setItemPropStatus("A/B/F", Status.Kind.modified);
        file = new File(thisTest.getWorkingCopy().getAbsolutePath(),"A/C/sigma");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("unversioned sigma");
        pw.close();
        thisTest.getWc().addItem("A/C/sigma", "unversioned sigma");
        thisTest.getWc().setItemTextStatus("A/C/sigma", Status.Kind.unversioned);
        thisTest.getWc().setItemNodeKind("A/C/sigma", NodeKind.unknown);
        file = new File(thisTest.getWorkingCopy().getAbsolutePath(), "A/C/Q");
        file.mkdir();
        thisTest.getWc().addItem("A/C/Q", null);
        thisTest.getWc().setItemNodeKind("A/C/Q", NodeKind.unknown);
        thisTest.getWc().setItemTextStatus("A/C/Q", Status.Kind.unversioned);
        file = new File(thisTest.getWorkingCopy().getAbsolutePath(), "A/B/X");
        client.mkdir(new String[] {file.getAbsolutePath()}, null);
        thisTest.getWc().addItem("A/B/X", null);
        thisTest.getWc().setItemTextStatus("A/B/X", Status.Kind.added);
        file = new File(file, "xi");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("added xi");
        pw.close();
        client.add(file.getAbsolutePath(),false);
        thisTest.getWc().addItem("A/B/X/xi", "added xi");
        thisTest.getWc().setItemTextStatus("A/B/X/xi", Status.Kind.added);
        file = new File(thisTest.getWorkingCopy().getAbsolutePath(), "A/B/Y");
        client.mkdir(new String[] {file.getAbsolutePath()}, null);
        thisTest.getWc().addItem("A/B/Y", null);
        thisTest.getWc().setItemTextStatus("A/B/Y", Status.Kind.added);
        thisTest.checkStatus();

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/H/chi"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/H"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/G/rho"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/G"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/F"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/C/sigma"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/C"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        try
        {
            client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/X"}, null, false);
            fail("missing exception");
        }
        catch(ClientException e)
        {
        }

        thisTest.checkStatus();

        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/E"}, null, false);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/H"}, null, true);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/G"}, null, true);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/F"}, null, true);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/C"}, null, true);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/X"}, null, true);
        file = new File(thisTest.getWorkingCopy(), "iota");
        file.delete();
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        file = new File(thisTest.getWorkingCopy(), "A/D/gamma");
        file.delete();
        client.remove(new String[] {file.getAbsolutePath()}, null, false);
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/B/E"}, null, false);
        thisTest.getWc().setItemTextStatus("A/D/H",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/chi",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/omega",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/H/psi",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/rho",Status.Kind.deleted);
        thisTest.getWc().setItemPropStatus("A/D/G/rho", Status.Kind.none);
        thisTest.getWc().setItemTextStatus("A/D/G/pi",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/G/tau",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/E",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/E/alpha",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/E/beta",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/B/F",Status.Kind.deleted);
        thisTest.getWc().setItemPropStatus("A/B/F", Status.Kind.none);
        thisTest.getWc().setItemTextStatus("A/C",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("iota",Status.Kind.deleted);
        thisTest.getWc().setItemTextStatus("A/D/gamma",Status.Kind.deleted);
        thisTest.getWc().removeItem("A/B/X");
        thisTest.getWc().removeItem("A/B/X/xi");
        thisTest.getWc().removeItem("A/C/sigma");
        thisTest.getWc().removeItem("A/C/Q");
        thisTest.checkStatus();
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D"},null, true);
        thisTest.getWc().setItemTextStatus("A/D", Status.Kind.deleted);
        thisTest.getWc().removeItem("A/D/Y");
        thisTest.checkStatus();

        assertFalse("failed to remove text modified file", new File(thisTest.getWorkingCopy(), "A/D/G/rho").exists());
        assertFalse("failed to remove prop modified file", new File(thisTest.getWorkingCopy(), "A/D/H/chi").exists());
        assertFalse("failed to remove unversioned file", new File(thisTest.getWorkingCopy(), "A/C/sigma").exists());
        assertFalse("failed to remove unmodified file", new File(thisTest.getWorkingCopy(), "A/B/E/alpha").exists());
        file = new File(thisTest.getWorkingCopy(),"A/B/F");
        assertTrue("removed versioned dir", file.exists() && file.isDirectory());
        assertFalse("failed to remove unversioned dir", new File(thisTest.getWorkingCopy(), "A/C/Q").exists());
        assertFalse("failed to remove added dir", new File(thisTest.getWorkingCopy(), "A/B/X").exists());

        file = new File(thisTest.getWorkingCopy().getAbsolutePath(),"foo");
        pw = new PrintWriter(new FileOutputStream(file));
        pw.print("unversioned foo");
        pw.close();
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        assertFalse("failed to remove unversioned file foo", file.exists());
        client.remove(new String[] {file.getAbsolutePath()}, null, true);
        client.remove(new String[] {thisTest.getURL()+"/iota"}, "delete iota URL", false);
    }

    public void testBasicCheckoutDeleted() throws Throwable
    {
        OneTest thisTest = new OneTest();
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D"}, null, true);
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
        thisTest.checkStatus();
        assertEquals("wrong revision from commit",client.commit(new String[]{thisTest.getWorkingCopy().getAbsolutePath()}, "log message", true),2);
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
        thisTest.checkStatus();
        client.checkout(thisTest.getURL()+"/A/D", thisTest.getWorkingCopy().getAbsolutePath()+"/new_D", new Revision.Number(1), true);
    }

    public void testBasicNodeKindChange() throws Throwable
    {
        OneTest thisTest = new OneTest();
        client.remove(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/gamma"}, null, false);
        thisTest.getWc().setItemTextStatus("A/D/gamma", Status.Kind.deleted);
        thisTest.checkStatus();
        try
        {
            client.mkdir(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/gamma"}, null);
            fail("can change node kind");
        }
        catch(ClientException e)
        {

        }
        thisTest.checkStatus();
        assertEquals("wrong revision number from commit", client.commit(new String[]{thisTest.getWorkingCopy().getAbsolutePath()},"log message", true), 2);
        thisTest.getWc().removeItem("A/D/gamma");
        thisTest.checkStatus();

        try
        {
            client.mkdir(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/gamma"}, null);
            fail("can change node kind");
        }
        catch(ClientException e)
        {

        }
        thisTest.checkStatus();
        client.update(thisTest.getWorkingCopy().getAbsolutePath(), null, true);
        thisTest.checkStatus();
        client.mkdir(new String[] {thisTest.getWorkingCopy().getAbsolutePath()+"/A/D/gamma"}, null);
        thisTest.getWc().addItem("A/D/gamma", null);
        thisTest.getWc().setItemTextStatus("A/D/gamma", Status.Kind.added);
        thisTest.checkStatus();
    }

    public void testBasicImport() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File file = new File(thisTest.getWorkingCopy().getAbsolutePath(),"new_file");
        PrintWriter pw = new PrintWriter(new FileOutputStream(file));
        pw.print("some text");
        pw.close();
        client.doImport(file.getAbsolutePath(), thisTest.getURL()+"/dirA/dirB/new_file","log message for new import", true);
        file.delete();
        assertEquals("wrong revision from commit",client.update(thisTest.getWorkingCopy().getAbsolutePath(), null, true),2);
        thisTest.getWc().addItem("dirA", null);
        thisTest.getWc().setItemWorkingCopyRevision("dirA",2);
        thisTest.getWc().addItem("dirA/dirB", null);
        thisTest.getWc().setItemWorkingCopyRevision("dirA/dirB",2);
        thisTest.getWc().addItem("dirA/dirB/new_file", "some text");
        thisTest.getWc().setItemWorkingCopyRevision("dirA/dirB/new_file",2);
        thisTest.checkStatus();
    }

    public void testBasicCat() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File mu = new File(thisTest.getWorkingCopy(), "A/mu");
        PrintWriter pw = new PrintWriter(new FileOutputStream(mu, true));
        pw.print("some text");
        pw.close();
        byte[] content = client.fileContent(thisTest.getWorkingCopy().getAbsolutePath()+"/A/mu", null);
        byte[] testContent = thisTest.getWc().getItemContent("A/mu").getBytes();
        assertTrue("content changed", Arrays.equals(content, testContent));
    }

    public void testBasicLs() throws Throwable
    {
        OneTest thisTest = new OneTest();

        DirEntry[] entries = client.list(thisTest.getWorkingCopy().getAbsolutePath(), null, false);
        thisTest.getWc().check(entries,"", false);
        entries = client.list(thisTest.getWorkingCopy().getAbsolutePath()+"/A", null, false);
        thisTest.getWc().check(entries,"A", false);
        entries = client.list(thisTest.getWorkingCopy().getAbsolutePath()+"/A", Revision.BASE, false);
        thisTest.getWc().check(entries,"A", false);
        entries = client.list(thisTest.getWorkingCopy().getAbsolutePath()+"/A/mu", null, false);
        thisTest.getWc().check(entries,"A/mu");
    }

    public void testBasicAddIgnores() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File dir = new File(thisTest.getWorkingCopy(), "dir");
        dir.mkdir();
        File fileC = new File(dir, "foo.c");
        new FileOutputStream(fileC).close();
        File fileO = new File(dir, "foo.o");
        new FileOutputStream(fileO).close();
        client.add(dir.getAbsolutePath(), true);
        thisTest.getWc().addItem("dir", null);
        thisTest.getWc().setItemTextStatus("dir",Status.Kind.added);
        thisTest.getWc().addItem("dir/foo.c", "");
        thisTest.getWc().setItemTextStatus("dir/foo.c",Status.Kind.added);
        thisTest.getWc().addItem("dir/foo.o", "");
        thisTest.getWc().setItemTextStatus("dir/foo.o",Status.Kind.ignored);
        thisTest.getWc().setItemNodeKind("dir/foo.o", NodeKind.unknown);
        thisTest.checkStatus();
    }

    public void testBasicImportIgnores() throws Throwable
    {
        OneTest thisTest = new OneTest();
        File dir = new File(thisTest.getWorkingCopy(), "dir");
        dir.mkdir();
        File fileC = new File(dir, "foo.c");
        new FileOutputStream(fileC).close();
        File fileO = new File(dir, "foo.o");
        new FileOutputStream(fileO).close();
        client.doImport(dir.getAbsolutePath(), thisTest.getURL()+"/dir","log message for import", true);
        removeDirectoryWithContent(dir);
        assertEquals("wrong revision from update", 2, client.update(thisTest.getWorkingCopy().getAbsolutePath(), null, true));
        thisTest.getWc().addItem("dir", null);
        thisTest.getWc().addItem("dir/foo.c", "");
        thisTest.checkStatus();
    */

/*
  'do not import ignored files in imported dirs'

  # The bug was that
  #
  #   $ svn import dir
  #
  # where dir contains some items that match the ignore list and some
  # do not would add all items, ignored or not.
  #
  # This has been fixed by testing each item with the new
  # svn_wc_is_ignored function.

  sbox.build()
  wc_dir = sbox.wc_dir

  dir_path = os.path.join(wc_dir, 'dir')
  foo_c_path = os.path.join(dir_path, 'foo.c')
  foo_o_path = os.path.join(dir_path, 'foo.o')

  os.mkdir(dir_path, 0755)
  open(foo_c_path, 'w')
  open(foo_o_path, 'w')

  # import new dir into repository
  url = svntest.main.current_repo_url + '/dir'

  output, errput = svntest.actions.run_and_verify_svn(
    None, None, [], 'import',
    '--username', svntest.main.wc_author,
    '--password', svntest.main.wc_passwd,
    '-m', 'Log message for new import',
    dir_path, url)

  lastline = string.strip(output.pop())
  cm = re.compile ("(Committed|Imported) revision [0-9]+.")
  match = cm.search (lastline)
  if not match:
    ### we should raise a less generic error here. which?
    raise svntest.actions.SVNUnexpectedOutput

  # remove (uncontrolled) local dir
  svntest.main.safe_rmtree(dir_path)

  # Create expected disk tree for the update (disregarding props)
  expected_disk = svntest.main.greek_state.copy()
  expected_disk.add({
    'dir/foo.c' : Item(''),
    })

  # Create expected status tree for the update (disregarding props).
  # Newly imported file should be at revision 2.
  expected_status = svntest.actions.get_virginal_state(wc_dir, 2)
  expected_status.add({
    'dir' : Item(status='  ', wc_rev=2, repos_rev=2),
    'dir/foo.c' : Item(status='  ', wc_rev=2, repos_rev=2),
    })

  # Create expected output tree for the update.
  expected_output = svntest.wc.State(wc_dir, {
    'dir' : Item(status='A '),
    'dir/foo.c' : Item(status='A '),
  })

  # do update and check three ways
  svntest.actions.run_and_verify_update(wc_dir,
                                        expected_output,
                                        expected_disk,
                                        expected_status,
                                        None, None, None,
                                        None, None, 1)

*/
    //}
}
