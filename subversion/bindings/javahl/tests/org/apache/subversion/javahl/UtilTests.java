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

import org.apache.subversion.javahl.types.ExternalItem;
import org.apache.subversion.javahl.types.NodeKind;
import org.apache.subversion.javahl.types.Revision;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.ByteArrayOutputStream;
import java.util.Arrays;
import java.util.List;
import java.util.ArrayList;

/**
 * Tests the JavaHL SVNUtil APIs.
 */
public class UtilTests extends SVNTests
{
    private final static String NL = System.getProperty("line.separator");

    /**
     * Base name of all our tests.
     */
    public final static String testName = "util_test";

    public UtilTests()
    {
        init();
    }

    public UtilTests(String name)
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

    private final byte[] fileContentsPrefix = "1\n2\n3\n4\n".getBytes();
    private final byte[] fileContentsSuffix = "N-3\nN-2\nN-1\nN\n".getBytes();

    private void writeFileContents(File file, String contents) throws Throwable
    {
        FileOutputStream out = new FileOutputStream(file);
        out.write(fileContentsPrefix);
        out.write(contents.getBytes());
        out.write(fileContentsSuffix);
        out.close();
    }

    public void testFileDiff() throws Throwable
    {
        File original = File.createTempFile("merge", ".original", localTmp);
        File modified = File.createTempFile("merge", ".modified", localTmp);

        writeFileContents(original, "\noriginal\n\n");
        writeFileContents(modified, "\nmodified\n\n");

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        boolean diffs =  SVNUtil.fileDiff(original.getAbsolutePath(),
                                          modified.getAbsolutePath(),
                                          null, "original", "modified", "UTF-8",
                                          null, result);
        assertTrue(diffs);

        final byte[] expected = ("--- original" + NL +
                                 "+++ modified" + NL +
                                 "@@ -3,7 +3,7 @@" + NL +
                                 " 3\n 4\n \n" +
                                 "-original\n" +
                                 "+modified\n" +
                                 " \n N-3\n N-2\n").getBytes();
        assertTrue(Arrays.equals(expected, result.toByteArray()));
    }

    public void testFileMerge() throws Throwable
    {
        File original = File.createTempFile("merge", ".original", localTmp);
        File modified = File.createTempFile("merge", ".modified", localTmp);
        File latest = File.createTempFile("merge", ".latest", localTmp);

        writeFileContents(original, "\noriginal\n\n");
        writeFileContents(modified, "\nmodified\n\n");
        writeFileContents(latest, "\nlatest\n\n");

        ByteArrayOutputStream result = new ByteArrayOutputStream();
        boolean conflicts = SVNUtil.fileMerge(original.getAbsolutePath(),
                                              modified.getAbsolutePath(),
                                              latest.getAbsolutePath(),
                                              null,
                                              "||||||| base",
                                              "<<<<<<< branch",
                                              ">>>>>>> local",
                                              "=======",
                                              SVNUtil.ConflictDisplayStyle.modified_original_latest,
                                              result);
        assertTrue(conflicts);

        final byte[] expected = ("1\n2\n3\n4\n\n" +
                                 "<<<<<<< branch\n" +
                                 "modified\n" +
                                 "||||||| base\n" +
                                 "original\n" +
                                 "=======\n" +
                                 "latest\n" +
                                 ">>>>>>> local\n" +
                                 "\nN-3\nN-2\nN-1\nN\n").getBytes();
        assertTrue(Arrays.equals(expected, result.toByteArray()));
    }

    public void testValidateProp() throws Throwable
    {
        File temp = File.createTempFile("propcheck", ".file", localTmp);
        FileOutputStream out = new FileOutputStream(temp);
        out.write("normal text\n".getBytes());
        out.close();

        byte[] prop = SVNUtil.canonicalizeNodeProperty(
                           "svn:eol-style", "  native".getBytes(),
                           "propcheck.file", NodeKind.file,
                           "text/plain");
        assertEquals("native", new String(prop));

        prop = SVNUtil.canonicalizeNodeProperty(
                    "svn:eol-style", " native  ".getBytes(),
                    "propcheck.file", NodeKind.file,
                    "text/plain", new FileInputStream(temp));
        assertEquals("native", new String(prop));

        boolean caught_exception = false;
        try {
            prop = SVNUtil.canonicalizeNodeProperty(
                        "svn:eol-style", " weird  ".getBytes(),
                        "propcheck.file", NodeKind.file,
                        "text/plain");
        } catch (ClientException ex) {
            assertEquals("Unrecognized line ending style",
                         ex.getAllMessages().get(0).getMessage());
            caught_exception = true;
        }
        assertTrue(caught_exception);

        out = new FileOutputStream(temp);
        out.write("inconsistent\r\ntext\n".getBytes());
        out.close();

        caught_exception = false;
        try {
            prop = SVNUtil.canonicalizeNodeProperty(
                        "svn:eol-style", " native  ".getBytes(),
                        "propcheck.file", NodeKind.file,
                        "text/plain", new FileInputStream(temp));
        } catch (ClientException ex) {
            assertEquals("Inconsistent line ending style",
                         ex.getAllMessages().get(2).getMessage());
            caught_exception = true;
        }
        assertTrue(caught_exception);
    }


    private static List<ExternalItem> externals = null;
    static {
        try {
            externals = new ArrayList<ExternalItem>(20);
            externals.add(new ExternalItem("a", "http://server/repo/path",
                                           null, null));
            externals.add(new ExternalItem("b", "//server/repo/path",
                                           null, null));
            externals.add(new ExternalItem("c", "/repo/path",
                                           null, null));
            externals.add(new ExternalItem("d", "^/path",
                                           null, null));
            externals.add(new ExternalItem("e", "^/../oper/path",
                                           null, null));

            externals.add(new ExternalItem("f", "http://server/repo/path",
                                           Revision.getInstance(42), null));
            externals.add(new ExternalItem("g", "//server/repo/path",
                                           Revision.getInstance(42), null));
            externals.add(new ExternalItem("h", "/repo/path",
                                           Revision.getInstance(42), null));
            externals.add(new ExternalItem("j", "^/path",
                                           Revision.getInstance(42), null));
            externals.add(new ExternalItem("j", "^/../oper/path",
                                           Revision.getInstance(42), null));

            externals.add(new ExternalItem("k", "http://server/repo/path",
                                           null, Revision.getInstance(42)));
            externals.add(new ExternalItem("l", "//server/repo/path",
                                           null, Revision.getInstance(42)));
            externals.add(new ExternalItem("m", "/repo/path",
                                           null, Revision.getInstance(42)));
            externals.add(new ExternalItem("n", "^/path",
                                           null, Revision.getInstance(42)));
            externals.add(new ExternalItem("o", "^/../oper/path",
                                           null, Revision.getInstance(42)));

            externals.add(new ExternalItem("p", "http://server/repo/path",
                                           Revision.getInstance(69),
                                           Revision.getInstance(71)));
            externals.add(new ExternalItem("q", "//server/repo/path",
                                           Revision.getInstance(69),
                                           Revision.getInstance(71)));
            externals.add(new ExternalItem("r", "/repo/path",
                                           Revision.getInstance(69),
                                           Revision.getInstance(71)));
            externals.add(new ExternalItem("s", "^/path",
                                           Revision.getInstance(69),
                                           Revision.getInstance(71)));
            externals.add(new ExternalItem("t", "^/../oper/path",
                                           Revision.getInstance(69),
                                           Revision.getInstance(71)));
        } catch (SubversionException ex) {
            externals = null;
            throw new RuntimeException(ex);
        }
    }

    public void testUnparseExternals() throws Throwable
    {
        byte[] props = SVNUtil.unparseExternals(externals, "dirname");
        assertEquals(424, props.length);
    }

    private static List<ExternalItem> old_externals = null;
    static {
        try {
            old_externals = new ArrayList<ExternalItem>(2);
            old_externals.add(new ExternalItem("X", "http://server/repo/path",
                                               null, null));
            old_externals.add(new ExternalItem("Y", "http://server/repo/path",
                                               Revision.getInstance(42), null));
        } catch (SubversionException ex) {
            old_externals = null;
            throw new RuntimeException(ex);
        }
    }

    public void testUnparseExternalsOldstyle() throws Throwable
    {
        byte[] props;

        props = SVNUtil.unparseExternalsForAncientUnsupportedClients(
                     old_externals, "dirname");
        assertEquals(57, props.length);

        // The fancy new features are not supported in the old format
        boolean caught_exception = false;
        try {
            props = SVNUtil.unparseExternalsForAncientUnsupportedClients(
                         externals, "dirname");
        } catch (SubversionException ex) {
            caught_exception = true;
        }
        assertTrue(caught_exception);
    }
}
