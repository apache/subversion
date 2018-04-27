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
import java.io.FileWriter;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Arrays;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.HashMap;

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
            externals = new ArrayList<ExternalItem>(25);
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

            externals.add(new ExternalItem("u", "http://server/repo/path",
                                           Revision.getInstance(42),
                                           Revision.getInstance(42)));
            externals.add(new ExternalItem("v", "//server/repo/path",
                                           Revision.getInstance(42),
                                           Revision.getInstance(42)));
            externals.add(new ExternalItem("w", "/repo/path",
                                           Revision.getInstance(42),
                                           Revision.getInstance(42)));
            externals.add(new ExternalItem("x", "^/path",
                                           Revision.getInstance(42),
                                           Revision.getInstance(42)));
            externals.add(new ExternalItem("y", "^/../oper/path",
                                           Revision.getInstance(42),
                                           Revision.getInstance(42)));
        } catch (SubversionException ex) {
            externals = null;
            throw new RuntimeException(ex);
        }
    }

    private static final byte[] externals_propval =
        ("http://server/repo/path a\n" +
         "//server/repo/path b\n" +
         "/repo/path c\n" +
         "^/path d\n" +
         "^/../oper/path e\n" +
         "-r42 http://server/repo/path f\n" +
         "-r42 //server/repo/path g\n" +
         "-r42 /repo/path h\n" +
         "-r42 ^/path j\n" +
         "-r42 ^/../oper/path j\n" +
         "http://server/repo/path@42 k\n" +
         "//server/repo/path@42 l\n" +
         "/repo/path@42 m\n" +
         "^/path@42 n\n" +
         "^/../oper/path@42 o\n" +
         "-r69 http://server/repo/path@71 p\n" +
         "-r69 //server/repo/path@71 q\n" +
         "-r69 /repo/path@71 r\n" +
         "-r69 ^/path@71 s\n" +
         "-r69 ^/../oper/path@71 t\n" +
         "http://server/repo/path@42 u\n" +
         "//server/repo/path@42 v\n" +
         "/repo/path@42 w\n" +
         "^/path@42 x\n" +
         "^/../oper/path@42 y\n").getBytes();

    private static List<ExternalItem> old_externals = null;
    static {
        try {
            old_externals = new ArrayList<ExternalItem>(3);
            old_externals.add(new ExternalItem("X", "http://server/repo/path",
                                               null, null));
            old_externals.add(new ExternalItem("Y", "http://server/repo/path",
                                               null, Revision.getInstance(42)));

            old_externals.add(new ExternalItem("Z", "http://server/repo/path",
                                               null, Revision.getInstance(new Date(0L))));
        } catch (SubversionException ex) {
            old_externals = null;
            throw new RuntimeException(ex);
        }
    }

    private static final byte[] old_externals_propval =
        ("X http://server/repo/path\n" +
         "Y -r42 http://server/repo/path\n" +
         "Z -r{1970-01-01T00:00:00.000000Z} http://server/repo/path\n").getBytes();

    private static void compare_item_lists(List<ExternalItem> a,
                                           List<ExternalItem> b,
                                           String list_name)
    {
        final int length = a.size();
        assertEquals(length, b.size());
        for (int i = 0; i < length; ++i)
            assertTrue("Items in " + list_name + " at index " + i + " differ",
                       a.get(i).equals(b.get(i)));
    }

    public void testParseExternals() throws Throwable
    {
        List<ExternalItem> items;

        items = SVNUtil.parseExternals(externals_propval, "dirname", false);
        compare_item_lists(items, externals, "externals");

        items = SVNUtil.parseExternals(old_externals_propval, "dirname", false);
        compare_item_lists(items, old_externals, "old_externals");
    }

    public void testUnparseExternals() throws Throwable
    {
        byte[] props = SVNUtil.unparseExternals(externals, "dirname");
        assertTrue(Arrays.equals(externals_propval, props));
    }

    public void testUnparseExternalsOldstyle() throws Throwable
    {
        byte[] props;

        props = SVNUtil.unparseExternalsForAncientUnsupportedClients(
                     old_externals, "dirname");
        assertTrue(Arrays.equals(old_externals_propval, props));

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

    public void testResolveExternalsUrl() throws Throwable
    {
        assertEquals("http://a/b/c",
                     SVNUtil.resolveExternalsUrl(
                         new ExternalItem("x", "http://a/b/c/", null, null),
                         "http://a", "http://a/b"));

        assertEquals("http://a/b/c",
                     SVNUtil.resolveExternalsUrl(
                         new ExternalItem("x", "^/b/c", null, null),
                         "http://a", "http://a/b"));

        assertEquals("http://a/b/c",
                     SVNUtil.resolveExternalsUrl(
                         new ExternalItem("x", "../b/c", null, null),
                         "http://a", "http://a/b"));

        assertEquals("http://a/b/c",
                     SVNUtil.resolveExternalsUrl(
                         new ExternalItem("x", "/b/c", null, null),
                         "http://a", "http://a/b"));

        assertEquals("http://a/b/c",
                     SVNUtil.resolveExternalsUrl(
                         new ExternalItem("x", "//a/b/c", null, null),
                         "http://a", "http://a/b"));
    }

    public void testBuildKeywords() throws Throwable
    {
        final byte[] kwval = "Id TEST=%H%_%b%_%u".getBytes();

        Map<String, byte[]> result;

        result = SVNUtil.buildKeywords(kwval, Revision.SVN_INVALID_REVNUM,
                                       null, null, null, null);
        assertEquals("   ", new String(result.get("Id")));
        assertEquals("     ", new String(result.get("TEST")));

        result = SVNUtil.buildKeywords(kwval, 42, "http://a/b/c",
                                       "http://a", new Date(1), "X");
        assertEquals("c 42 1970-01-01 00:00:00Z X",
                     new String(result.get("Id")));
        assertEquals("b/c 42 1970-01-01 00:00:00Z X c http://a/b/c",
                     new String(result.get("TEST")));
    }

    public void testTranslateStream() throws Throwable
    {
        final byte[] keywordsValue = "Id TEST=%H%_%b%_%u".getBytes();
        final byte[] contentsContracted = "$Id$\n$TEST$\n".getBytes();
        final byte[] contentsExpanded =
            ("$Id: c 42 1970-01-01 00:00:00Z X $\r" +
             "$TEST: b/c 42 1970-01-01 00:00:00Z X c http://a/b/c $\r"
             ) .getBytes();
        final Map<String, byte[]> keywords =
            SVNUtil.buildKeywords(keywordsValue, 42, "http://a/b/c",
                                  "http://a", new Date(1), "X");
        final Map<String, byte[]> null_keywords = new HashMap<String, byte[]>();
        byte[] buffer = new byte[1024];

        for (Map.Entry<String, byte[]> e : keywords.entrySet())
            null_keywords.put(e.getKey(), null);

        // InputStream; expand
        InputStream testin = null;
        try {
            testin = SVNUtil.translateStream(
                         new ByteArrayInputStream(contentsContracted),
                         SVNUtil.EOL_CR, true, keywords, true);
            final int size = testin.read(buffer);
            testin.close();
            testin = null;

            assertEquals("expand InputStream with concrete keywords",
                         new String(contentsExpanded),
                         new String(buffer, 0, size));
        } finally {
            if (testin != null) {
                testin.close();
                testin = null;
            }
        }

        try {
            testin = SVNUtil.translateStream(
                         new ByteArrayInputStream(contentsContracted),
                         SVNUtil.EOL_CR, true, true,
                         keywordsValue, 42, "http://a/b/c",
                         "http://a", new Date(1), "X");
            final int size = testin.read(buffer);
            testin.close();
            testin = null;

            assertEquals("expand InputStream with implied keywords",
                         new String(contentsExpanded),
                         new String(buffer, 0, size));
        } finally {
            if (testin != null) {
                testin.close();
                testin = null;
            }
        }

        // InputStream; contract
        try {
            testin = SVNUtil.translateStream(
                         new ByteArrayInputStream(contentsExpanded),
                         SVNUtil.EOL_LF, true, null_keywords, false);
            final int size = testin.read(buffer);
            testin.close();
            testin = null;

            assertEquals("contract InputStream with concrete keywords",
                         new String(contentsContracted),
                         new String(buffer, 0, size));
        } finally {
            if (testin != null) {
                testin.close();
                testin = null;
            }
        }

        try {
            testin = SVNUtil.translateStream(
                         new ByteArrayInputStream(contentsExpanded),
                         SVNUtil.EOL_LF, true, false,
                         keywordsValue, 0, "", "", new Date(1), "");
            final int size = testin.read(buffer);
            testin.close();
            testin = null;

            assertEquals("contract InputStream with implied keywords",
                         new String(contentsContracted),
                         new String(buffer, 0, size));
        } finally {
            if (testin != null) {
                testin.close();
                testin = null;
            }
        }


        // OutputStream; expand
        OutputStream testout = null;
        try {
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            testout = SVNUtil.translateStream(
                         result, SVNUtil.EOL_CR, true, keywords, true);
            testout.write(contentsContracted);
            testout.close();
            testout = null;

            assertEquals("expand OutputStream with concrete keywords",
                         new String(contentsExpanded), result.toString());
        } finally {
            if (testout != null) {
                testout.close();
                testout = null;
            }
        }

        try {
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            testout = SVNUtil.translateStream(
                         result, SVNUtil.EOL_CR, true, true,
                         keywordsValue, 42, "http://a/b/c",
                         "http://a", new Date(1), "X");
            testout.write(contentsContracted);
            testout.close();
            testout = null;

            assertEquals("expand OutputStream with implied keywords",
                         new String(contentsExpanded), result.toString());
        } finally {
            if (testout != null) {
                testout.close();
                testout = null;
            }
        }

        // OutputStream; contract
        try {
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            testout = SVNUtil.translateStream(
                         result, SVNUtil.EOL_LF, true, null_keywords, false);
            testout.write(contentsExpanded);
            testout.close();
            testout = null;

            assertEquals("contract OutputStream with concrete keywords",
                         new String(contentsContracted), result.toString());
        } finally {
            if (testout != null) {
                testout.close();
                testout = null;
            }
        }

        try {
            ByteArrayOutputStream result = new ByteArrayOutputStream();
            testout = SVNUtil.translateStream(
                         result, SVNUtil.EOL_LF, true, false,
                         keywordsValue, 0, "", "", new Date(1), "");
            testout.write(contentsExpanded);
            testout.close();
            testout = null;

            assertEquals("contract OutputStream with implied keywords",
                         new String(contentsContracted), result.toString());
        } finally {
            if (testout != null) {
                testout.close();
                testout = null;
            }
        }
    }


    // Credentials definitions for testing the credentials utilities
    private static final String util_cred_hash =
        "4d7de6b1e103fbfc5e61565223ca23be";

    private static final String util_cred_username =
        "K 8\n" +
        "username\n" +
        "V 5\n" +
        "mungo\n" +
        "K 15\n" +
        "svn:realmstring\n" +
        "V 27\n" +
        "https://svn.example.com:443\n" +
        "END\n";

    private static final String util_cred_simple =
        "K 8\n" +
        "passtype\n" +
        "V 10\n" +
        "javahltest\n" +
        "K 8\n" +
        "username\n" +
        "V 5\n" +
        "mungo\n" +
        "K 8\n" +
        "password\n" +
        "V 6\n" +
        "secret\n" +
        "K 15\n" +
        "svn:realmstring\n" +
        "V 27\n" +
        "https://svn.example.com:443\n" +
        "END\n";

    private static final String util_cred_ssl_server =
        "K 10\n" +
        "ascii_cert\n" +
        "V 1616\n" +
        "MIIEtzCCA5+gAwIBAgIQWGBOrapkezd+BWVsAtmtmTANBgkqhkiG9w0BAQsFADA8" +
        "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMVGhhd3RlLCBJbmMuMRYwFAYDVQQDEw1U" +
        "aGF3dGUgU1NMIENBMB4XDTE0MDQxMTAwMDAwMFoXDTE2MDQwNzIzNTk1OVowgYsx" +
        "CzAJBgNVBAYTAlVTMREwDwYDVQQIEwhNYXJ5bGFuZDEUMBIGA1UEBxQLRm9yZXN0" +
        "IEhpbGwxIzAhBgNVBAoUGkFwYWNoZSBTb2Z0d2FyZSBGb3VuZGF0aW9uMRcwFQYD" +
        "VQQLFA5JbmZyYXN0cnVjdHVyZTEVMBMGA1UEAxQMKi5hcGFjaGUub3JnMIIBIjAN" +
        "BgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA+Tq4mH+stRoxe4xth8tUCgLt+P4L" +
        "D/JWZz4a2IecaaAk57vIlTxEyP16fUShUfxVJnD0KV11zv2qaEUXNaA6hKd4H/oB" +
        "u2OyGev+quRM+aFCjWqASkXt7fLGsIkHAwP3XwBVBpARbcXJeCjCBxqaYrQqS8LT" +
        "wfPUD9eYncGlQ+ixb3Bosy7TmkWKeLsRdS90cAO/rdgQ8OI7kLT/1tr5GpF9RmXo" +
        "RnVqMP+U0zGd/BNNSneg7emb7TxLzxeMKZ7QbF4MZi8RRN11spvx8/f92CiYrGGu" +
        "y67VdOGPaomYc+VZ2syLwduHGK40ADrEK3+MQpsRFB0dM08j9bhpr5A44wIDAQAB" +
        "o4IBYzCCAV8wFwYDVR0RBBAwDoIMKi5hcGFjaGUub3JnMAkGA1UdEwQCMAAwQgYD" +
        "VR0gBDswOTA3BgpghkgBhvhFAQc2MCkwJwYIKwYBBQUHAgEWG2h0dHBzOi8vd3d3" +
        "LnRoYXd0ZS5jb20vY3BzLzAOBgNVHQ8BAf8EBAMCBaAwHwYDVR0jBBgwFoAUp6KD" +
        "uzRFQD381TBPErk+oQGf9tswOgYDVR0fBDMwMTAvoC2gK4YpaHR0cDovL3N2ci1v" +
        "di1jcmwudGhhd3RlLmNvbS9UaGF3dGVPVi5jcmwwHQYDVR0lBBYwFAYIKwYBBQUH" +
        "AwEGCCsGAQUFBwMCMGkGCCsGAQUFBwEBBF0wWzAiBggrBgEFBQcwAYYWaHR0cDov" +
        "L29jc3AudGhhd3RlLmNvbTA1BggrBgEFBQcwAoYpaHR0cDovL3N2ci1vdi1haWEu" +
        "dGhhd3RlLmNvbS9UaGF3dGVPVi5jZXIwDQYJKoZIhvcNAQELBQADggEBAF52BLvl" +
        "x5or9/aO7+cPhxuPxwiNRgbvHdCakD7n8vzjNyct9fKp6/XxB6GQiTZ0nZPJOyIu" +
        "Pi1QDLKOXvaPeLKDBilL/+mrn/ev3s/aRQSrUsieKDoQnqtmlxEHc/T3+Ni/RZob" +
        "PD4GzPuNKpK3BIc0fk/95T8R1DjBSQ5/clvkzOKtcl3VffAwnHiE9TZx9js7kZwO" +
        "b9nOKX8DFao3EpQcS7qn63Ibzbq5A6ry8ZNRQSIJK/xlCAWoyUd1uxnqGFnus8wb" +
        "9RVZJQe8YvyytBjgbE3QjnfPOxoEJA3twupnPmH+OCTM6V3TZqpRZj/sZ5rtIQ++" +
        "hI5FdJWUWVSgnSw=\n" +
        "K 8\n" +
        "failures\n" +
        "V 1\n" +
        "8\n" +
        "K 15\n" +
        "svn:realmstring\n" +
        "V 26\n" +
        "https://svn.apache.org:443\n" +
        "END\n";

    private static final String util_cred_ssl_client_passphrase =
        "K 8\n" +
        "passtype\n" +
        "V 10\n" +
        "javahltest\n" +
        "K 10\n" +
        "passphrase\n" +
        "V 24\n" +
        "secret with spaces in it\n" +
        "K 15\n" +
        "svn:realmstring\n" +
        "V 27\n" +
        "https://svn.example.com:443\n" +
        "END\n";

    // Initialize credentials used for testing credentials utilities
    private void initCredentials() throws Throwable
    {
        File auth = new File(this.conf, "auth");
        if (auth.exists())
            removeDirOrFile(auth);
        auth.mkdirs();

        File store = new File(auth, "svn.username");
        store.mkdir();
        FileWriter cred = new FileWriter(new File(store, util_cred_hash));
        cred.write(util_cred_username);
        cred.close();

        store = new File(auth, "svn.simple");
        store.mkdir();
        cred = new FileWriter(new File(store, util_cred_hash));
        cred.write(util_cred_simple);
        cred.close();

        store = new File(auth, "svn.ssl.server");
        store.mkdir();
        cred = new FileWriter(new File(store, util_cred_hash));
        cred.write(util_cred_ssl_server);
        cred.close();

        store = new File(auth, "svn.ssl.client-passphrase");
        store.mkdir();
        cred = new FileWriter(new File(store, util_cred_hash));
        cred.write(util_cred_ssl_client_passphrase);
        cred.close();
    }

    /**
     * Test credentials search.
     */
    public void testCredentials() throws Throwable
    {
        initCredentials();

        final String configDir = this.conf.getAbsolutePath();
        SVNUtil.Credential cred;
        List<SVNUtil.Credential> creds;

        /* one username credential */
        cred = SVNUtil.getCredential(configDir,
                                     SVNUtil.Credential.Kind.username,
                                     "https://svn.example.com:443");
        assertNotNull(cred);
        assertEquals(cred.getUsername(), "mungo");

        /* one simple credential */
        cred = SVNUtil.getCredential(configDir,
                                     SVNUtil.Credential.Kind.simple,
                                     "https://svn.example.com:443");
        assertNotNull(cred);
        assertEquals(cred.getUsername(), "mungo");
        assertEquals(cred.getPassword(), "secret");
        assertEquals(cred.getSecureStore(), "javahltest");

        /* one SSL server trust credential */
        cred = SVNUtil.getCredential(configDir,
                                     SVNUtil.Credential.Kind.sslServer,
                                     "https://svn.apache.org:443");
        assertNotNull(cred);
        assertEquals(cred.getServerCertInfo().getSubject(),
                     "C=US, ST=Maryland, L=Forest Hill, " +
                     "O=Apache Software Foundation, OU=Infrastructure, " +
                     "CN=*.apache.org");

        /* one SSL client passphrase credential */
        cred = SVNUtil.getCredential(configDir,
                                     SVNUtil.Credential.Kind.sslClientPassphrase,
                                     "https://svn.example.com:443");
        assertNotNull(cred);
        assertEquals(cred.getClientCertPassphrase(), "secret with spaces in it");
        assertEquals(cred.getSecureStore(), "javahltest");

        /* search with no parameters (empty return) */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          null, null, null, null);
        assertNull(creds);

        /* search with unmatched parameters */
        creds = SVNUtil.searchCredentials(configDir,
                                          SVNUtil.Credential.Kind.sslServer,
                                          null, null, null, "*java*");
        assertNull(creds);

        /* search with unmatched parameters */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          null, "*java*", null, null);
        assertNull(creds);

        /* search with match on kind */
        creds = SVNUtil.searchCredentials(configDir,
                                          SVNUtil.Credential.Kind.sslServer,
                                          "*", null, null, null);
        assertNotNull(creds);
        assertEquals(creds.size(), 1);

        /* search with match on passtype */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          null, null, null, "*java*");
        assertNotNull(creds);
        assertEquals(creds.size(), 2);

        /* search with match on username */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          null, "mungo", null, null);
        assertNotNull(creds);
        assertEquals(creds.size(), 2);

        /* search with match on subvject */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          null, null, "\\*.apache.org", null);
        assertNotNull(creds);
        assertEquals(creds.size(), 1);

        /* search with match on realm */
        creds = SVNUtil.searchCredentials(configDir, null,
                                          "*example*", null, null, null);
        assertNotNull(creds);
        assertEquals(creds.size(), 3);
    }
}
