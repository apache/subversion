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
    protected OneTest thisTest;

    public SVNRemoteTests()
    {
    }

    public SVNRemoteTests(String name)
    {
        super(name);
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
            factory.setPassword(PASSWORD);
            factory.setPrompt(new DefaultPromptUserPassword());

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
            session = new RemoteFactory(
                super.conf.getAbsolutePath(),
                USERNAME, PASSWORD,
                new DefaultPromptUserPassword(), null)
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

    private final class CommitContext implements CommitCallback
    {
        public final ISVNEditor editor;
        public CommitContext(ISVNRemote session, String logstr)
            throws ClientException
        {
            Charset UTF8 = Charset.forName("UTF-8");
            byte[] log = (logstr == null
                          ? new byte[0]
                          : logstr.getBytes(UTF8));
            HashMap<String, byte[]> revprops = new HashMap<String, byte[]>();
            revprops.put("svn:log", log);
            editor = session.getCommitEditor(revprops, this, null, false);
        }

        public void commitInfo(CommitInfo info) { this.info = info; }
        public long getRevision() { return info.getRevision(); }

        private CommitInfo info;
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

        try {
            SVNUtil.setConfigEventHandler(handler);
            ISVNRemote session = getSession();
            session.getLatestRevision();
        } finally {
            SVNUtil.setConfigEventHandler(null);
        }
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
        ArrayList<RevisionRange> merge_range = new ArrayList<RevisionRange>(1);
        merge_range.add(new RevisionRange(Revision.START, Revision.START));
        client.merge(getTestRepoUrl() + "/A", Revision.HEAD, merge_range,
                     thisTest.getWCPath() + "/Abranch", false, Depth.infinity,
                     false, false, false);
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
    }
}
