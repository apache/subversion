/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */
package org.apache.subversion.javahl;

import org.apache.subversion.javahl.*;
import org.apache.subversion.javahl.remote.*;
import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.types.*;

import java.util.Arrays;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Set;
import java.util.Map;
import java.util.HashMap;
import java.util.HashSet;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.charset.Charset;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/**
 * This class is used for testing the SVNReposAccess class
 *
 * More methodes for testing are still needed
 */
public class SVNRemoteTests extends SVNTests
{
    /**
     * Base name of all our tests.
     */
    public final static String testName = "remote_test";

    protected OneTest thisTest;

    public SVNRemoteTests()
    {
        init();
    }

    public SVNRemoteTests(String name)
    {
        super(name);
        init();
    }

    /**
     * Initialize the testBaseName and the testCounter, if this is the
     * first test of this class.
     */
    private void init()
    {
        if (!testName.equals(testBaseName))
        {
            testCounter = 0;
            testBaseName = testName;
        }
    }

    protected void setUp() throws Exception
    {
        super.setUp();

        thisTest = new OneTest();
    }

    public static ISVNRemote getSession(String url, String configDirectory)
    {
        try
        {
            RemoteFactory factory = new RemoteFactory();
            factory.setConfigDirectory(configDirectory);
            factory.setUsername(USERNAME);
            // Do not set default password, exercise prompter instead.
            if (DefaultAuthn.useDeprecated())
                factory.setPrompt(DefaultAuthn.getDeprecated());
            else
                factory.setPrompt(DefaultAuthn.getDefault());

            ISVNRemote raSession = factory.openRemoteSession(url);
            assertNotNull("Null session was returned by factory", raSession);
            return raSession;
        }
        catch (Exception ex)
        {
            throw new RuntimeException(ex);
        }
    }

    private ISVNRemote getSession()
    {
        return getSession(getTestRepoUrl(), super.conf.getAbsolutePath());
    }

    /**
     * Test the basic SVNAdmin.create functionality
     * @throws SubversionException
     */
    public void testCreate()
        throws SubversionException, IOException
    {
        assertTrue("repository exists", thisTest.getRepository().exists());
    }

    public void testGetSession_ConfigConstructor() throws Exception
    {
        ISVNRemote session;
        try
        {
            if (DefaultAuthn.useDeprecated())
                session = new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDeprecated(),
                    null, null, null)
                    .openRemoteSession(getTestRepoUrl());
            else
                session = new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDefault(),
                    null, null, null)
                    .openRemoteSession(getTestRepoUrl());
        }
        catch (ClientException ex)
        {
            throw new RuntimeException(ex);
        }
        assertNotNull("Null session was returned by factory", session);
        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testDispose() throws Exception
    {
        ISVNRemote session = getSession();
        session.dispose();
    }

    public void testSessionGC() throws Exception
    {
        int svnErrorCode = 0;
        try {
            try {
                String prefix = getTestRepoUrl().substring(
                    0, 1 + getTestRepoUrl().lastIndexOf("/"));

            if (DefaultAuthn.useDeprecated())
                new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDeprecated(),
                    null, null, null)
                    .openRemoteSession(prefix + "repositorydoesnotexisthere");
            else
                new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDefault(),
                    null, null, null)
                    .openRemoteSession(prefix + "repositorydoesnotexisthere");
            }
            finally
            {
                for(int i = 0; i < 100; i++)
                {
                    Runtime.getRuntime().gc(); // GC should run finalize

                    // Do something
                    byte[] memEater = new byte[1024 * 1024];
                    Arrays.fill(memEater, (byte) i);

                    // Do some more javahl activity (this url is OK)
                    final ISVNRemote session = getSession();
                    session.getLatestRevision();
                    session.dispose();
                }
            }
        }
        catch (ClientException ex)
        {
            List<ClientException.ErrorMessage> msgs = ex.getAllMessages();
            svnErrorCode = msgs.get(msgs.size() - 1).getCode();
        }

        assertTrue(svnErrorCode == 180001    // file:
                   || svnErrorCode == 210005 // svn:
                   || svnErrorCode == 2);    // http:
    }

    public void testDatedRev() throws Exception
    {
        ISVNRemote session = getSession();

        long revision = session.getRevisionByDate(new Date());
        assertEquals(revision, 1);
    }

    public void testGetLocks() throws Exception
    {
        ISVNRemote session = getSession();

        Set<String> iotaPathSet = new HashSet<String>(1);
        String iotaPath = thisTest.getWCPath() + "/iota";
        iotaPathSet.add(iotaPath);

        client.lock(iotaPathSet, "foo", false);

        Map<String, Lock> locks = session.getLocks("iota", Depth.infinity);

        assertEquals(locks.size(), 1);
        Lock lock = locks.get("/iota");
        assertNotNull(lock);
        assertEquals(lock.getOwner(), "jrandom");
    }

    public void testCheckPath() throws Exception
    {
        ISVNRemote session = getSession();

        NodeKind kind = session.checkPath("iota", 1);
        assertEquals(NodeKind.file, kind);

        kind = session.checkPath("iota", 0);
        assertEquals(NodeKind.none, kind);

        kind = session.checkPath("A", 1);
        assertEquals(NodeKind.dir, kind);
    }

    public void testStat() throws Exception
    {
        ISVNRemote session = getSession();

        DirEntry dirent = session.stat("iota", 1);
        assertEquals(NodeKind.file, dirent.getNodeKind());

        dirent = session.stat("iota", 0);
        assertNull(dirent);

        dirent = session.stat("A", 1);
        assertEquals(NodeKind.dir, dirent.getNodeKind());
    }

    private String getTestRepoUrl()
    {
        return thisTest.getUrl().toASCIIString();
    }

    public void testGetLatestRevision() throws Exception
    {
        ISVNRemote session = getSession();
        long revision = session.getLatestRevision();
        assertEquals(revision, 1);
    }

    public void testGetUUID() throws Exception
    {
        ISVNRemote session = getSession();

        /*
         * Test UUID
         * TODO: Test for actual UUID once test dump file has
         * fixed UUID
         */
        assertNotNull(session.getReposUUID());
    }

    public void testGetUrl() throws Exception
    {
        ISVNRemote session = getSession();

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testGetRootUrl() throws Exception
    {
        ISVNRemote session = getSession();
        session.reparent(session.getSessionUrl() + "/A/B/E");
        assertEquals(getTestRepoUrl(), session.getReposRootUrl());
    }

    public void testGetUrl_viaSVNClient() throws Exception
    {
        ISVNRemote session = client.openRemoteSession(getTestRepoUrl());

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testGetUrl_viaSVNClientWorkingCopy() throws Exception
    {
        ISVNRemote session = client.openRemoteSession(thisTest.getWCPath());

        assertEquals(getTestRepoUrl(), session.getSessionUrl());
    }

    public void testReparent() throws Exception
    {
        ISVNRemote session = getSession();
        String newUrl = session.getSessionUrl() + "/A/B/E";
        session.reparent(newUrl);
        assertEquals(newUrl, session.getSessionUrl());
    }

    public void testGetRelativePath() throws Exception
    {
        ISVNRemote session = getSession();
        String baseUrl = session.getSessionUrl() + "/A/B/E";
        session.reparent(baseUrl);

        String relPath = session.getSessionRelativePath(baseUrl + "/alpha");
        assertEquals("alpha", relPath);

        relPath = session.getReposRelativePath(baseUrl + "/beta");
        assertEquals("A/B/E/beta", relPath);
    }

    public void testGetCommitEditor() throws Exception
    {
        ISVNRemote session = getSession();
        session.getCommitEditor(null, null, null, false);
    }

    public void testDisposeCommitEditor() throws Exception
    {
        ISVNRemote session = getSession();
        session.getCommitEditor(null, null, null, false);
        session.dispose();
    }

    public void testHasCapability() throws Exception
    {
        ISVNRemote session = getSession();
        assert(session.hasCapability(ISVNRemote.Capability.depth));
    }

    public void testChangeRevpropNoAtomic() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        boolean atomic =
            session.hasCapability(ISVNRemote.Capability.atomic_revprops);

        if (atomic)
            return;

        boolean exceptioned = false;
        try
        {
            byte[] oldValue = "bumble".getBytes(UTF8);
            byte[] newValue = "bee".getBytes(UTF8);
            session.changeRevisionProperty(1, "svn:author",
                                           oldValue, newValue);
        }
        catch (IllegalArgumentException ex)
        {
            exceptioned = true;
        }
        assert(exceptioned);
    }

    public void testChangeRevpropAtomic() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        boolean atomic =
            session.hasCapability(ISVNRemote.Capability.atomic_revprops);

        if (!atomic)
            return;

        byte[] oldValue = client.revProperty(getTestRepoUrl(), "svn:author",
                                             Revision.getInstance(1));
        byte[] newValue = "rayjandom".getBytes(UTF8);
        try
        {
            session.changeRevisionProperty(1, "svn:author",
                                           oldValue, newValue);
        }
        catch (ClientException ex)
        {
            ClientException.ErrorMessage error = null;
            for (ClientException.ErrorMessage m : ex.getAllMessages())
                if (!m.isGeneric()) {
                    error = m;
                    break;
                }

            if (error == null)
                fail("Failed with no error message");

            if (error.getCode() != 175002 && // SVN_ERR_RA_DAV_REQUEST_FAILED
                error.getCode() != 165006)   // SVN_ERR_REPOS_DISABLED_FEATURE
                fail(error.getMessage());

            return;
        }

        byte[] check = client.revProperty(getTestRepoUrl(), "svn:author",
                                          Revision.getInstance(1));
        assertTrue(Arrays.equals(check, newValue));
    }

    public void testGetRevpropList() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        Map<String, byte[]> proplist = session.getRevisionProperties(1);
        assertTrue(Arrays.equals(proplist.get("svn:author"),
                                 USERNAME.getBytes(UTF8)));
    }

    public void testGetRevprop() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        byte[] propval = session.getRevisionProperty(1, "svn:author");
        assertTrue(Arrays.equals(propval, USERNAME.getBytes(UTF8)));
    }

    public void testGetFile() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        ByteArrayOutputStream contents = new ByteArrayOutputStream();
        HashMap<String, byte[]> properties = new HashMap<String, byte[]>();
        properties.put("fakename", "fakecontents".getBytes(UTF8));
        long fetched_rev =
            session.getFile(Revision.SVN_INVALID_REVNUM, "A/B/lambda",
                            contents, properties);
        assertEquals(fetched_rev, 1);
        assertEquals(contents.toString("UTF-8"),
                     "This is the file 'lambda'.");
        for (Map.Entry<String, byte[]> e : properties.entrySet()) {
            final String key = e.getKey();
            assertTrue(key.startsWith("svn:entry:")
                       || key.startsWith("svn:wc:"));
        }
    }

    public void testGetDirectory() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        HashMap<String, DirEntry> dirents = new HashMap<String, DirEntry>();
        dirents.put("E", null);
        dirents.put("F", null);
        dirents.put("lambda", null);
        HashMap<String, byte[]> properties = new HashMap<String, byte[]>();
        properties.put("fakename", "fakecontents".getBytes(UTF8));
        long fetched_rev =
            session.getDirectory(Revision.SVN_INVALID_REVNUM, "A/B",
                                 DirEntry.Fields.all, dirents, properties);
        assertEquals(fetched_rev, 1);
        assertEquals(dirents.get("E").getPath(), "E");
        assertEquals(dirents.get("F").getPath(), "F");
        assertEquals(dirents.get("lambda").getPath(), "lambda");
        for (Map.Entry<String, byte[]> e : properties.entrySet()) {
            final String key = e.getKey();
            assertTrue(key.startsWith("svn:entry:")
                       || key.startsWith("svn:wc:"));
        }
    }

    private static final class CommitContext implements CommitCallback
    {
        public final ISVNEditor editor;
        public CommitContext(ISVNRemote session, String logstr,
                             ISVNEditor.ProvideBaseCallback getBase,
                             ISVNEditor.ProvidePropsCallback getProps,
                             ISVNEditor.GetNodeKindCallback getKind)
            throws ClientException
        {
            Charset UTF8 = Charset.forName("UTF-8");
            byte[] log = (logstr == null
                          ? new byte[0]
                          : logstr.getBytes(UTF8));
            HashMap<String, byte[]> revprops = new HashMap<String, byte[]>();
            revprops.put("svn:log", log);

            // Run the getCommitEditor overloads through their paces, too.
            if (getBase == null && getProps == null && getKind == null)
                editor = session.getCommitEditor(revprops, this, null, false);
            else
                editor = session.getCommitEditor(revprops, this, null, false,
                                                 getBase, getProps, getKind);
        }

        public CommitContext(ISVNRemote session, String logstr)
            throws ClientException
        {
            this(session, logstr, null, null, null);
        }

        public void commitInfo(CommitInfo info) { this.info = info; }
        public long getRevision() { return info.getRevision(); }

        private CommitInfo info;
    }

    private static final class EditorCallbacks
    {
        private final String wcpath;
        private final long revision;
        private final Map<String, byte[]> props;
        private final NodeKind kind;

        public EditorCallbacks(String wcpath, long revision,
                               Map<String, byte[]> props,
                               NodeKind kind)
        {
            this.wcpath = wcpath;
            this.revision = revision;
            this.props = props;
            this.kind = kind;
        }

        public final ISVNEditor.ProvideBaseCallback getBase =
            new ISVNEditor.ProvideBaseCallback()
            {
                public ISVNEditor.ProvideBaseCallback.ReturnValue
                getContents(String relpath)
                {
                    try {
                        return new ISVNEditor.ProvideBaseCallback.ReturnValue(
                            new FileInputStream(wcpath + relpath), revision);
                    } catch (java.io.FileNotFoundException e) {
                        throw new RuntimeException(e);
                    }
                }
            };

        public final ISVNEditor.ProvidePropsCallback getProps =
            new ISVNEditor.ProvidePropsCallback()
            {
                public ISVNEditor.ProvidePropsCallback.ReturnValue
                getProperties(String relpath)
                {
                    return new ISVNEditor.ProvidePropsCallback.ReturnValue(
                        props, revision);
                }
            };

        public final ISVNEditor.GetNodeKindCallback getKind =
            new ISVNEditor.GetNodeKindCallback()
            {
                public NodeKind getKind(String relpath, long revision)
                {
                    return kind;
                }
            };
    };

    private void testEditorCopy(EditorCallbacks cb) throws Exception
    {
        ISVNRemote session = getSession();
        CommitContext cc =
            (cb != null
             ? new CommitContext(session, "Copy A/B/lambda -> A/B/omega",
                                 cb.getBase, cb.getProps, cb.getKind)
             : new CommitContext(session, "Copy A/B/lambda -> A/B/omega"));

        try {
            // FIXME: alter dir A/B first
            cc.editor.copy("A/B/lambda", 1, "A/B/omega",
                           Revision.SVN_INVALID_REVNUM);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        assertEquals(NodeKind.file,
                     session.checkPath("A/B/lambda",
                                       Revision.SVN_INVALID_REVNUM));
        assertEquals(NodeKind.file,
                     session.checkPath("A/B/omega",
                                       Revision.SVN_INVALID_REVNUM));
    }

    public void testEditorCopy() throws Exception
    {
        testEditorCopy(null);
    }

    public void testEditorCopy_WithCallbacks() throws Exception
    {
        testEditorCopy(new EditorCallbacks(thisTest.getWCPath(), 1L,
                                           new HashMap<String, byte[]>(),
                                           NodeKind.file));
    }

    public void testEditorMove() throws Exception
    {
        ISVNRemote session = getSession();
        CommitContext cc =
            new CommitContext(session, "Move A/B/lambda -> A/B/omega");

        try {
            // FIXME: alter dir A/B first
            cc.editor.move("A/B/lambda", 1, "A/B/omega",
                           Revision.SVN_INVALID_REVNUM);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        assertEquals(NodeKind.none,
                     session.checkPath("A/B/lambda",
                                       Revision.SVN_INVALID_REVNUM));
        assertEquals(NodeKind.file,
                     session.checkPath("A/B/omega",
                                       Revision.SVN_INVALID_REVNUM));
    }

    public void testEditorDelete() throws Exception
    {
        ISVNRemote session = getSession();
        CommitContext cc =
            new CommitContext(session, "Delete all greek files");

        String[] filePaths = { "iota",
                               "A/mu",
                               "A/B/lambda",
                               "A/B/E/alpha",
                               "A/B/E/beta",
                               "A/D/gamma",
                               "A/D/G/pi",
                               "A/D/G/rho",
                               "A/D/G/tau",
                               "A/D/H/chi",
                               "A/D/H/omega",
                               "A/D/H/psi" };

        try {
            // FIXME: alter a bunch of dirs first
            for (String path : filePaths)
                cc.editor.delete(path, 1);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        for (String path : filePaths)
            assertEquals(NodeKind.none,
                         session.checkPath(path, Revision.SVN_INVALID_REVNUM));
    }

    public void testEditorMkdir() throws Exception
    {
        ISVNRemote session = getSession();
        CommitContext cc = new CommitContext(session, "Make hebrew dir");

        try {
            // FIXME: alter dir . first
            cc.editor.addDirectory("ALEPH",
                                   new ArrayList<String>(),
                                   new HashMap<String, byte[]>(),
                                   Revision.SVN_INVALID_REVNUM);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        assertEquals(NodeKind.dir,
                     session.checkPath("ALEPH",
                                       Revision.SVN_INVALID_REVNUM));
    }

    private void testEditorSetDirProps(EditorCallbacks cb) throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        byte[] ignoreval = "*.pyc\n.gitignore\n".getBytes(UTF8);
        byte[] binaryval = new byte[]{(byte)0, (byte)13, (byte)255, (byte)8,
                                      (byte)127, (byte)128, (byte)129};
        HashMap<String, byte[]> props = new HashMap<String, byte[]>();
        props.put("svn:ignore", ignoreval);
        props.put("binaryprop", binaryval);

        CommitContext cc =
            (cb != null
             ? new CommitContext(session, "Add svn:ignore and binaryprop",
                                 cb.getBase, cb.getProps, cb.getKind)
             : new CommitContext(session, "Add svn:ignore and binaryprop"));
        try {
            cc.editor.alterDirectory("", 1, null, props);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        assertTrue(Arrays.equals(ignoreval,
                                 client.propertyGet(session.getSessionUrl(),
                                                    "svn:ignore",
                                                    Revision.HEAD,
                                                    Revision.HEAD)));
        assertTrue(Arrays.equals(binaryval,
                                 client.propertyGet(session.getSessionUrl(),
                                                    "binaryprop",
                                                    Revision.HEAD,
                                                    Revision.HEAD)));
    }

    public void testEditorSetDirProps() throws Exception
    {
        testEditorSetDirProps(null);
    }

    public void testEditorSetDirProps_WithCallbacks() throws Exception
    {
        testEditorSetDirProps(new EditorCallbacks(thisTest.getWCPath(), 1L,
                                                  new HashMap<String, byte[]>(),
                                                  NodeKind.dir));
    }

    private static byte[] SHA1(byte[] text) throws NoSuchAlgorithmException
    {
        MessageDigest md = MessageDigest.getInstance("SHA-1");
        return md.digest(text);
    }

    public void testEditorAddFile() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        byte[] eolstyle = "native".getBytes(UTF8);
        HashMap<String, byte[]> props = new HashMap<String, byte[]>();
        props.put("svn:eol-style", eolstyle);

        byte[] contents = "This is file 'xi'.".getBytes(UTF8);
        Checksum hash = new Checksum(SHA1(contents), Checksum.Kind.SHA1);
        ByteArrayInputStream stream = new ByteArrayInputStream(contents);

        CommitContext cc = new CommitContext(session, "Add A/xi");
        try {
            // FIXME: alter dir A first
            cc.editor.addFile("A/xi", hash, stream, props,
                              Revision.SVN_INVALID_REVNUM);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        assertEquals(NodeKind.file,
                     session.checkPath("A/xi",
                                       Revision.SVN_INVALID_REVNUM));

        byte[] propval = client.propertyGet(session.getSessionUrl() + "/A/xi",
                                            "svn:eol-style",
                                            Revision.HEAD,
                                            Revision.HEAD);
        assertTrue(Arrays.equals(eolstyle, propval));
    }

    public void testEditorDeleteFileProps() throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        client.propertySetRemote(
             thisTest.getUrl() + "/iota", 1L,
             "name", "value".getBytes(UTF8),
             new CommitMessageCallback() {
                 public String getLogMessage(Set<CommitItem> elements) {
                     return "Set property 'name' to 'value'";
                 }
             }, false, null, null);

        ISVNRemote session = getSession();
        HashMap<String, byte[]> props = new HashMap<String, byte[]>();
        assertEquals(2L, session.getFile(Revision.SVN_INVALID_REVNUM, "iota",
                                         null, props));

        int propcount = 0;
        for (Map.Entry<String, byte[]> e : props.entrySet()) {
            final String key = e.getKey();
            if (key.startsWith("svn:entry:") || key.startsWith("svn:wc:"))
                continue;
            ++propcount;
        }
        assertEquals(1, propcount);

        CommitContext cc = new CommitContext(session, "Remove all props");
        try {
            props.clear();
            cc.editor.alterFile("iota", 2L, null, null, props);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(3L, session.getFile(Revision.SVN_INVALID_REVNUM, "iota",
                                         null, props));
        propcount = 0;
        for (Map.Entry<String, byte[]> e : props.entrySet()) {
            final String key = e.getKey();
            if (key.startsWith("svn:entry:") || key.startsWith("svn:wc:"))
                continue;
            ++propcount;
        }
        assertEquals(0, propcount);
    }

    private void testEditorSetFileContents(EditorCallbacks cb) throws Exception
    {
        Charset UTF8 = Charset.forName("UTF-8");
        ISVNRemote session = getSession();

        byte[] contents = "This is modified file 'alpha'.".getBytes(UTF8);
        Checksum hash = new Checksum(SHA1(contents), Checksum.Kind.SHA1);
        ByteArrayInputStream stream = new ByteArrayInputStream(contents);

        CommitContext cc =
            (cb != null
             ? new CommitContext(session, "Change contents of A/B/E/alpha",
                                 cb.getBase, cb.getProps, cb.getKind)
             : new CommitContext(session, "Change contents of A/B/E/alpha"));
        try {
            cc.editor.alterFile("A/B/E/alpha", 1, hash, stream, null);
            cc.editor.complete();
        } finally {
            cc.editor.dispose();
        }

        assertEquals(2, cc.getRevision());
        assertEquals(2, session.getLatestRevision());
        ByteArrayOutputStream checkcontents = new ByteArrayOutputStream();
        client.streamFileContent(session.getSessionUrl() + "/A/B/E/alpha",
                                 Revision.HEAD, Revision.HEAD, checkcontents);
        assertTrue(Arrays.equals(contents, checkcontents.toByteArray()));
    }

    public void testEditorSetFileContents() throws Exception
    {
        testEditorSetFileContents(null);
    }

    public void testEditorSetFileContents_WithCallbacks() throws Exception
    {
        testEditorSetFileContents(new EditorCallbacks(thisTest.getWCPath(), 1L,
                                                      new HashMap<String, byte[]>(),
                                                      NodeKind.file));
    }

    // Sanity check so that we don't forget about unimplemented methods.
    public void testEditorNotImplemented() throws Exception
    {
        ISVNRemote session = getSession();

        HashMap<String, byte[]> props = new HashMap<String, byte[]>();
        // ArrayList<ISVNEditor.RotatePair> rotation =
        //     new ArrayList<ISVNEditor.RotatePair>();

        CommitContext cc = new CommitContext(session, "not implemented");
        try {
            String exmsg;

            try {
                exmsg = "";
                cc.editor.addSymlink("", "", props, 1);
            } catch (RuntimeException ex) {
                exmsg = ex.getMessage();
            }
            assertEquals("Not implemented: CommitEditor.addSymlink", exmsg);

            try {
                exmsg = "";
                cc.editor.alterSymlink("", 1, "", null);
            } catch (RuntimeException ex) {
                exmsg = ex.getMessage();
            }
            assertEquals("Not implemented: CommitEditor.alterSymlink", exmsg);

        } finally {
            cc.editor.dispose();
        }
    }

    private static final class LogMsg
    {
        public Set<ChangePath> changedPaths;
        public long revision;
        public Map<String, byte[]> revprops;
        public boolean hasChildren;
    }

    private static final class LogReceiver implements LogMessageCallback
    {
        public final ArrayList<LogMsg> logs = new ArrayList<LogMsg>();

        public void singleMessage(Set<ChangePath> changedPaths,
                                  long revision,
                                  Map<String, byte[]> revprops,
                                  boolean hasChildren)
        {
            LogMsg msg = new LogMsg();
            msg.changedPaths = changedPaths;
            msg.revision = revision;
            msg.revprops = revprops;
            msg.hasChildren = hasChildren;
            logs.add(msg);
        }
    }

    public void testGetLog() throws Exception
    {
        ISVNRemote session = getSession();
        LogReceiver receiver = new LogReceiver();

        session.getLog(null,
                       Revision.SVN_INVALID_REVNUM,
                       Revision.SVN_INVALID_REVNUM,
                       0, false, false, false, null,
                       receiver);
        assertEquals(1, receiver.logs.size());
        assertTrue(receiver.logs.get(0).revprops.size() > 0);

        receiver.logs.clear();
        session.reparent(getTestRepoUrl() + "/A");
        session.getLog(null,
                       Revision.SVN_INVALID_REVNUM,
                       0, 0, false, false, false, null,
                       receiver);
        assertEquals(2, receiver.logs.size());
        assertTrue(receiver.logs.get(0).revprops.size() > 0);
    }

    public void testGetLogMissing() throws Exception
    {
        ISVNRemote session = getSession();
        LogReceiver receiver = new LogReceiver();

        ArrayList<String> paths = new ArrayList<String>(1);
        paths.add("X");

        boolean exception = false;
        try {
            session.getLog(paths,
                           Revision.SVN_INVALID_REVNUM,
                           Revision.SVN_INVALID_REVNUM,
                           0, false, false, false, null,
                           receiver);
        } catch (ClientException ex) {
            assertEquals("Filesystem has no item",
                         ex.getAllMessages().get(0).getMessage());
            exception = true;
        }

        assertEquals(0, receiver.logs.size());
        assertTrue(exception);
    }

    public void testConfigHandler() throws Exception
    {
        ConfigEvent handler = new ConfigEvent()
            {
                public void onLoad(ISVNConfig cfg)
                {
                    //System.out.println("config:");
                    onecat(cfg.config());
                    //System.out.println("servers:");
                    onecat(cfg.servers());
                }

                private void onecat(ISVNConfig.Category cat)
                {
                    for (String sec : cat.sections()) {
                        //System.out.println("  [" + sec + "]");
                        ISVNConfig.Enumerator en = new ISVNConfig.Enumerator()
                            {
                                public void option(String name, String value)
                                {
                                    //System.out.println("    " + name
                                    //                   + " = " + value);
                                }
                            };
                        cat.enumerate(sec, en);
                    }
                }

            };

        ISVNRemote session;
        try
        {
            if (DefaultAuthn.useDeprecated())
                session = new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDeprecated(),
                    null, handler, null)
                    .openRemoteSession(getTestRepoUrl());
            else
                session = new RemoteFactory(
                    super.conf.getAbsolutePath(),
                    USERNAME, null, // Do not set default password.
                    DefaultAuthn.getDefault(),
                    null, handler, null)
                    .openRemoteSession(getTestRepoUrl());
        }
        catch (ClientException ex)
        {
            throw new RuntimeException(ex);
        }
        session.getLatestRevision();
    }

    private static class RemoteStatusReceiver implements RemoteStatus
    {
        static class StatInfo implements Comparable<StatInfo>
        {
            public String relpath = null;
            public char kind = ' '; // F, D, L
            public boolean textChanged = false;
            public boolean propsChanged = false;
            public boolean deleted = false;
            public Entry info = null;

            StatInfo(String relpath, char kind, boolean added)
            {
                this.relpath = relpath;
                this.kind = kind;
                this.deleted = !added;
            }

            StatInfo(String relpath, char kind,
                     boolean textChanged, boolean propsChanged,
                     Entry info)
            {
                this.relpath = relpath;
                this.kind = kind;
                this.textChanged = textChanged;
                this.propsChanged = propsChanged;
                this.info = info;
            }

            @Override
            public boolean equals(Object statinfo)
            {
                final StatInfo that = (StatInfo)statinfo;
                return this.relpath.equals(that.relpath);
            }

            @Override
            public int hashCode()
            {
                return this.relpath.hashCode();
            }

            @Override
            public int compareTo(StatInfo that)
            {
                return this.relpath.compareTo(that.relpath);
            }
        }

        private boolean debug;

        public RemoteStatusReceiver()
        {
            this.debug = false;
        }

        public RemoteStatusReceiver(boolean debug)
        {
            this.debug = debug;
        }

        public ArrayList<StatInfo> status = new ArrayList<StatInfo>();

        public void addedDirectory(String relativePath)
        {
            if (debug)
                System.err.println("RemoteStatus:  A   (dir)  " +
                                   relativePath);
            status.add(new StatInfo(relativePath, 'D', true));
        }

        public void addedFile(String relativePath)
        {
            if (debug)
                System.err.println("RemoteStatus:  A   (file) "
                                   + relativePath);
            status.add(new StatInfo(relativePath, 'F', true));
        }

        public void addedSymlink(String relativePath)
        {
            if (debug)
                System.err.println("RemoteStatus:  A   (link) "
                                   + relativePath);
            status.add(new StatInfo(relativePath, 'L', true));
        }

        public void modifiedDirectory(String relativePath,
                                      boolean childrenModified,
                                      boolean propsModified,
                                      Entry nodeInfo)
        {
            if (debug)
                System.err.println("RemoteStatus:  " +
                                   (childrenModified ? 'M' : '_') +
                                   (propsModified ? 'M' : '_') +
                                   "  (dir)  " + relativePath);
            status.add(new StatInfo(relativePath, 'D',
                                    childrenModified, propsModified,
                                    nodeInfo));
        }

        public void modifiedFile(String relativePath,
                                 boolean textModified,
                                 boolean propsModified,
                                 Entry nodeInfo)
        {
            if (debug)
                System.err.println("RemoteStatus:  " +
                                   (textModified ? 'M' : '_') +
                                   (propsModified ? 'M' : '_') +
                                   "  (file) " + relativePath);
            status.add(new StatInfo(relativePath, 'F',
                                    textModified, propsModified,
                                    nodeInfo));
        }

        public void modifiedSymlink(String relativePath,
                                    boolean targetModified,
                                    boolean propsModified,
                                    Entry nodeInfo)
        {
            if (debug)
                System.err.println("RemoteStatus:  " +
                                   (targetModified ? 'M' : '_') +
                                   (propsModified ? 'M' : '_') +
                                   "  (link) " + relativePath);
            status.add(new StatInfo(relativePath, 'L',
                                    targetModified, propsModified,
                                    nodeInfo));

        }

        public void deleted(String relativePath)
        {
            if (debug)
                System.err.println("RemoteStatus:  D          "
                                   + relativePath);
            status.add(new StatInfo(relativePath, ' ', false));
        }
    }

    public void testSimpleStatus() throws Exception
    {
        ISVNRemote session = getSession();

        RemoteStatusReceiver receiver = new RemoteStatusReceiver();
        ISVNReporter rp = session.status(null, Revision.SVN_INVALID_REVNUM,
                                         Depth.infinity, receiver);
        try {
            rp.setPath("", 0, Depth.infinity, true, null);
            assertEquals(1, rp.finishReport());
        } finally {
            rp.dispose();
        }
        assertEquals(21, receiver.status.size());
        session.checkPath("", Revision.SVN_INVALID_REVNUM);
    }

    public void testTextchangeStatus() throws Exception
    {
        ISVNRemote session = getSession();

        CommitMessageCallback cmcb = new CommitMessageCallback() {
                public String getLogMessage(Set<CommitItem> x) {
                    return "Content change on A/B/E/alpha";
                }
            };

        File alpha = new File(thisTest.getWorkingCopy(), "A/B/E/alpha");
        FileOutputStream writer = new FileOutputStream(alpha);
        writer.write("changed alpha text".getBytes());
        writer.close();
        client.commit(thisTest.getWCPathSet(), Depth.infinity, false, false,
                      null, null, cmcb, null);

        RemoteStatusReceiver receiver = new RemoteStatusReceiver();
        ISVNReporter rp = session.status(null, Revision.SVN_INVALID_REVNUM,
                                         Depth.infinity, receiver);
        try {
            rp.setPath("", 1, Depth.infinity, false, null);
            assertEquals(2, rp.finishReport());
        } finally {
            rp.dispose();
        }

        assertEquals(5, receiver.status.size());

        // ra_serf returns the entries in inverted order compared to ra_local.
        Collections.sort(receiver.status);
        RemoteStatusReceiver.StatInfo mod = receiver.status.get(4);
        assertEquals("A/B/E/alpha", mod.relpath);
        assertEquals('F', mod.kind);
        assertEquals("Text Changed", true, mod.textChanged);
        assertEquals("Props Changed", false, mod.propsChanged);
        assertEquals("Node Deleted", false, mod.deleted);
        assertEquals(2, mod.info.getCommittedRevision());
    }

    public void testPropchangeStatus() throws Exception
    {
        ISVNRemote session = getSession();

        CommitMessageCallback cmcb = new CommitMessageCallback() {
                public String getLogMessage(Set<CommitItem> x) {
                    return "Property change on A/D/gamma";
                }
            };
        client.propertySetRemote(getTestRepoUrl() + "/A/D/gamma",
                                 1L, "foo", "bar".getBytes(), cmcb,
                                 false, null, null);

        RemoteStatusReceiver receiver = new RemoteStatusReceiver();
        ISVNReporter rp = session.status(null, Revision.SVN_INVALID_REVNUM,
                                         Depth.infinity, receiver);
        try {
            rp.setPath("", 1, Depth.infinity, false, null);
            assertEquals(2, rp.finishReport());
        } finally {
            rp.dispose();
        }

        assertEquals(4, receiver.status.size());

        // ra_serf returns the entries in inverted order compared to ra_local.
        Collections.sort(receiver.status);
        RemoteStatusReceiver.StatInfo mod = receiver.status.get(3);
        assertEquals("A/D/gamma", mod.relpath);
        assertEquals('F', mod.kind);
        assertEquals("TextChanged", false, mod.textChanged);
        assertEquals("Props Changed", true, mod.propsChanged);
        assertEquals("Node Deleted", false, mod.deleted);
        assertEquals(2, mod.info.getCommittedRevision());
    }

    public void testDeletedStatus() throws Exception
    {
        ISVNRemote session = getSession();

        CommitMessageCallback cmcb = new CommitMessageCallback() {
                public String getLogMessage(Set<CommitItem> x) {
                    return "Delete A/mu";
                }
            };
        HashSet<String> paths = new HashSet<String>(1);
        paths.add(getTestRepoUrl() + "/A/mu");
        client.remove(paths, false, false, null, cmcb, null);

        RemoteStatusReceiver receiver = new RemoteStatusReceiver();
        ISVNReporter rp = session.status(null, Revision.SVN_INVALID_REVNUM,
                                         Depth.infinity, receiver);
        try {
            rp.setPath("", 1, Depth.infinity, false, null);
            assertEquals(2, rp.finishReport());
        } finally {
            rp.dispose();
        }
        assertEquals(3, receiver.status.size());

        // ra_serf returns the entries in inverted order compared to ra_local.
        Collections.sort(receiver.status);
        RemoteStatusReceiver.StatInfo mod = receiver.status.get(2);
        assertEquals("A/mu", mod.relpath);
        assertEquals(' ', mod.kind);
        assertEquals(false, mod.textChanged);
        assertEquals(false, mod.propsChanged);
        assertEquals(true, mod.deleted);
    }

    public void testTrivialMergeinfo() throws Exception
    {
        ISVNRemote session = getSession();
        ArrayList<String> paths = new ArrayList<String>(1);
        paths.add("");

        Map<String, Mergeinfo> catalog =
            session.getMergeinfo(paths, 1L, Mergeinfo.Inheritance.explicit,
                                 false);
        assertEquals(null, catalog);
    }

    public void testBranchMergeinfo() throws Exception
    {
        CommitMessageCallback cmcb = new CommitMessageCallback() {
                public String getLogMessage(Set<CommitItem> x) {
                    return "testBranchMergeinfo";
                }
            };

        ISVNRemote session = getSession();

        // Create a branch
        ArrayList<CopySource> dirA = new ArrayList<CopySource>(1);
        dirA.add(new CopySource(getTestRepoUrl() + "/A",
                                Revision.HEAD, Revision.HEAD));
        client.copy(dirA, getTestRepoUrl() + "/Abranch",
                    false, false, true, null, cmcb, null);

        // Check mergeinfo on new branch
        ArrayList<String> paths = new ArrayList<String>(1);
        paths.add("Abranch");
        Map<String, Mergeinfo> catalog =
            session.getMergeinfo(paths, 2L, Mergeinfo.Inheritance.explicit,
                                 false);
        assertEquals(null, catalog);

        // Modify source and merge to branch
        client.propertySetRemote(getTestRepoUrl() + "/A/D/gamma",
                                 2L, "foo", "bar".getBytes(), cmcb,
                                 false, null, null);
        client.update(thisTest.getWCPathSet(), Revision.HEAD, Depth.infinity,
                      false, false, true, false);
        client.merge(getTestRepoUrl() + "/A", Revision.HEAD, null,
                     thisTest.getWCPath() + "/Abranch", false, Depth.infinity,
                     false, false, false, false);
        client.commit(thisTest.getWCPathSet(), Depth.infinity, false, false,
                      null, null, cmcb, null);

        // Check inherited mergeinfo on updated branch
        paths.set(0, "Abranch/mu");
        catalog = session.getMergeinfo(paths, 4L,
                                       Mergeinfo.Inheritance.nearest_ancestor,
                                       false);
        assertEquals(1, catalog.size());
        List<RevisionRange> ranges =
            catalog.get("Abranch/mu").getRevisions("/A/mu");
        assertEquals(1, ranges.size());
        assertEquals("1-3", ranges.get(0).toString());
    }

    public void testGetLocations() throws Exception
    {
        ISVNRemote session = getSession();

        Long expected = new Long(1L);
        ArrayList<Long> revs = new ArrayList<Long>(3);
        revs.add(new Long(0L));
        revs.add(expected);

        Map<Long, String> locs = session.getLocations("A", 1, revs);

        assertEquals(1, locs.size());
        assertTrue(locs.containsKey(expected));
        assertEquals("/A", locs.get(expected));
    }

    public void testGetLocationSegments() throws Exception
    {
        ISVNRemote session = getSession();

        List<ISVNRemote.LocationSegment> result =
            session.getLocationSegments("A", 1,
                                        Revision.SVN_INVALID_REVNUM,
                                        Revision.SVN_INVALID_REVNUM);
        assertEquals(1, result.size());
        ISVNRemote.LocationSegment seg = result.get(0);
        assertEquals("A", seg.getPath());
        assertEquals(1, seg.getStartRevision());
        assertEquals(1, seg.getEndRevision());
    }

    public void testGetFileRevisions() throws Exception
    {
        ISVNRemote session = getSession();

        List<ISVNRemote.FileRevision> result =
            session.getFileRevisions("iota", 0, 1, true);
        assertEquals(1, result.size());
        ISVNRemote.FileRevision rev = result.get(0);
        assertEquals("/iota", rev.getPath());
        assertFalse(rev.isResultOfMerge());
        assertTrue(rev.hasTextDelta());
    }

    // This test is a result of a threading bug that was identified in
    // serf-1.3.2 and earlier. The net result was that opening two RA
    // sessions to an https:// URL in two parallel threads would cause
    // a crash in serf, due to the OpenSSL library not being
    // initialized in a single-threaded context.
    //
    // The problem does not appear to exist with other RA methods, but
    // the test is here just in case someone is actually pedantic
    // enough to test JavaHL with an HTTPS setup.
    public void testParallelOpen() throws Exception
    {
        final Runnable runnable = new Runnable() {
                @Override
                public void run() {
                    ISVNRemote session = null;
                    try {
                        session = getSession();
                        assertEquals(1, session.getLatestRevision());
                    }
                    catch (ClientException ex) {
                        throw new RuntimeException(ex);
                    }
                    finally {
                        if (session != null)
                            session.dispose();
                    }
                }
            };

        Thread thread1 = new Thread(runnable);
        Thread thread2 = new Thread(runnable);

        thread1.start();
        thread2.start();

        thread1.join();
        thread2.join();
    }
}
