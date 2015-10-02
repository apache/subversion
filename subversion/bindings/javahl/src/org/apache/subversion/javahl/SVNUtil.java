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

import org.apache.subversion.javahl.callback.*;
import org.apache.subversion.javahl.types.*;
import org.apache.subversion.javahl.util.*;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.Date;
import java.util.List;
import java.util.Map;

public class SVNUtil
{
    //
    // Global configuration
    //
    private static final ConfigLib configLib = new ConfigLib();

    /**
     * Enable storing authentication credentials in Subversion's
     * standard credentials store in the configuration directory and
     * system-specific secure locations.
     * <p>
     * The standard credentials store is enabled by default.
     * <p>
     * This setting will be inherited by all ISVNClient and ISVNRemote
     * objects. Changing the setting will not affect existing such
     * objects.
     * @throws ClientException
     */
    public static void enableNativeCredentialsStore()
        throws ClientException
      {
          configLib.enableNativeCredentialsStore();
      }

    /**
     * Disable storing authentication credentials in Subversion's
     * standard credentials store in the configuration directory and
     * system-specific secure locations. In this mode, the
     * authentication (see {@link ISVNClient#setPrompt} and {@link
     * remote.RemoteFactory#setPrompt}) will be called every time the
     * underlying library needs access to the credentials.
     * <p>
     * This mode is intented to support client implementations that
     * use their own credentials store.
     * <p>
     * The standard credentials store is enabled by default.
     * <p>
     * This setting will be inherited by all ISVNClient and ISVNRemote
     * objects. Changing the setting will not affect existing such
     * objects.
     * @throws ClientException
     */
    public static void disableNativeCredentialsStore()
        throws ClientException
      {
          configLib.disableNativeCredentialsStore();
      }

    /**
     * Find out if the standard credentials store is enabled.
     */
    public static boolean isNativeCredentialsStoreEnabled()
        throws ClientException
      {
          return configLib.isNativeCredentialsStoreEnabled();
      }

    //
    // Credentials management
    //

    /**
     * Exception used by calling the wrong accessor on Credential for
     * the given credential type.
     */
    public static class CredentialTypeMismatch extends SubversionException
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        public CredentialTypeMismatch(Credential.Kind kind, String attribute)
        {
            super("Credential type '" + kind.toString()
                  + "'  does not have the attribute '" + attribute + "'");
        }
    }

    /**
     * Generic credential description. Provides default accessors for
     * concrete implementations.
     */
    public static class Credential implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Describes the kind of the credential.
         */
        public static enum Kind
        {
            /** The username for a realm. */
            username            ("svn.username"),

            /** The username and password for a realm. */
            simple              ("svn.simple"),

            /** The trusted SSL server certificate for a realm. */
            sslServer           ("svn.ssl.server"),

            /** The client certificate passphrase for a realm. */
            sslClientPassphrase ("svn.ssl.client-passphrase");

            private String token;

            Kind(String token)
            {
                this.token = token;
            }

            /** @return the string representation of the enumeration. */
            public String toString()
            {
                return this.token;
            }

            /* Factory used by the native implementation */
            private static Kind fromString(String stringrep)
            {
                for (Kind kind : Kind.values()) {
                    if (kind.toString().equals(stringrep))
                        return kind;
                }
                return null;
            }
        }

        /** @return the kind of the credential. */
        public Kind getKind()
        {
            return kind;
        }

        /** @return the realm that the credential is valid for. */
        public String getRealm()
        {
            return realm;
        }

        /**
         * @return the type of the secure store used for the secret
         * parts of this credential; may be <code>null</code> if the
         * credential does not contain any secrets bits.
         */
        public String getSecureStore()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.simple && kind != Kind.sslClientPassphrase)
                throw new CredentialTypeMismatch(kind, "secure store");

            return store;
        }

        /**
         * @return the username associated with the credential, or
         * <code>null</code>, if there is no username in the concrete
         * credential type.
         */
        public String getUsername()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.username && kind != Kind.simple)
                throw new CredentialTypeMismatch(kind, "username");

            return username;
        }

        /**
         * @return the password associated with the credential, or
         * <code>null</code>, if there is no password in the concrete
         * credential type.
         */
        public String getPassword()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.simple && kind != Kind.sslClientPassphrase)
                throw new CredentialTypeMismatch(kind, "password");

            return password;
        }

        /**
         * @return the server certificate info associated with the
         * credential, or <code>null</code>, if there is no server
         * certificate in the concrete credential type.
         */
        public AuthnCallback.SSLServerCertInfo getServerCertInfo()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.sslServer)
                throw new CredentialTypeMismatch(kind, "server cert info");

            return info;
        }

        /**
         * @return the accepted server certificate failures associated
         * with the credential, or <code>null</code>, if there is no
         * server certificate in the concrete credential type.
         */
        public AuthnCallback.SSLServerCertFailures getServerCertFailures()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.sslServer)
                throw new CredentialTypeMismatch(kind, "server cert failures");

            return failures;
        }

        /**
         * @return the client certificate passphrase associated with
         * the credential, or <code>null</code>, if there is no client
         * certificate in the concrete credential type.
         */
        public String getClientCertPassphrase()
            throws CredentialTypeMismatch
        {
            if (kind != Kind.sslClientPassphrase)
                throw new CredentialTypeMismatch(kind, "passphrase");

            return passphrase;
        }

        // ### TODO: There are currently no proper APIs in Subversion
        //           for adding credentials. These factory methods are
        //           placeholders.
        //
        ///**
        // * Creates an "svn.username" credential.
        // * @param realm The realm string.
        // * @param username The username for <code>realm</code>.
        // */
        //public static Credential
        //    createUsername(String realm, String username)
        //{
        //    return new Credential(Kind.username, realm, null,
        //                          username, null, null, null, null);
        //}
        //
        ///**
        // * Creates an "svn.simple" credential.
        // * @param realm The realm string.
        // * @param username The username for <code>realm</code>.
        // * @param password The password for <code>username</code>.
        // */
        //public static Credential
        //    createSimple(String realm, String username, String password)
        //{
        //    return new Credential(Kind.simple, realm, null,
        //                          username, password, null, null, null);
        //}
        //
        ///** Creates an "svn.ssl.server" credential. */
        //public static Credential
        //    createSSLServerCertTrust(String realm,
        //                             AuthnCallback.SSLServerCertInfo info,
        //                             AuthnCallback.SSLServerCertFailures failures)
        //{
        //    return new Credential(Kind.sslServer, realm, null,
        //                          null, null, info, failures, null);
        //}
        //
        ///**
        // * Creates an "svn.ssl.client-passphrase" credential.
        // * @param realm The realm string.
        // * @param passphrase The passphrase for for the client certificate
        // *        used for <code>realm</code>.
        // */
        //public static Credential
        //    createSSLClientCertPassphrase(String realm, String passphrase)
        //{
        //    return new Credential(Kind.simple, realm, null,
        //                          null, null, null, null, passphrase);
        //}

        private Credential(Kind kind, String realm, String store,
                           String username, String password,
                           AuthnCallback.SSLServerCertInfo info,
                           AuthnCallback.SSLServerCertFailures failures,
                           String passphrase)
        {
            assert(kind != null && realm != null);
            switch (kind) {
            case username:
                assert(username != null && password == null
                       && info == null && failures == null
                       && passphrase == null);
                break;
            case simple:
                assert(username != null && password != null
                       && info == null && failures == null
                       && passphrase == null);
                break;
            case sslServer:
                assert(username == null && password == null
                       && info != null && failures != null
                       && passphrase == null);
                break;
            case sslClientPassphrase:
                assert(username == null && password == null
                       && info == null && failures == null
                       && passphrase != null);
                break;
            default:
                assert(kind == Kind.username
                       || kind == Kind.simple
                       || kind == Kind.sslServer
                       || kind == Kind.sslClientPassphrase);
            }

            this.kind = kind;
            this.realm = realm;
            this.store = store;
            this.username = username;
            this.password = password;
            this.info = info;
            this.failures = failures;
            this.passphrase = passphrase;
        }

        private Kind kind;
        private String realm;
        private String store;
        private String username;
        private String password;
        private AuthnCallback.SSLServerCertInfo info;
        private AuthnCallback.SSLServerCertFailures failures;
        private String passphrase;
    }

    /**
     * Find a stored credential.
     * Unlike {@link #searchCredentials}, the the realm name is not
     * a glob pattern.
     * <p>
     * <b>Note:</b> If the native credentials store is disabled, this
     *              method will always return <code>null</code>.
     *
     * @param configDir The path to the configuration directory; if
     *        <code>null</code>, the default (system-specific) user
     *        configuration path will be used.
     * @param kind The kind of the credential; may not be <code>null</code>.
     * @param realm The realm name; may not be <code>null</code>.
     * @return the matching credential, or <code>null</code> if not found.
     */
    public static Credential getCredential(String configDir,
                                           Credential.Kind kind,
                                           String realm)
        throws ClientException, SubversionException
    {
        return configLib.getCredential(configDir, kind, realm);
    }

    /**
     * Remove a stored credential.
     * Unlike {@link #deleteCredentials}, the the realm name is not
     * a glob pattern.
     * <p>
     * <b>Note:</b> If the native credentials store is disabled, this
     *              method will always return <code>null</code>.
     *
     * @param configDir The path to the configuration directory; if
     *        <code>null</code>, the default (system-specific) user
     *        configuration path will be used.
     * @param kind The kind of the credential; may not be <code>null</code>.
     * @param realm The realm name; may not be <code>null</code>.
     * @return the deleted credential, or <code>null</code> if not found.
     */
    public static Credential removeCredential(String configDir,
                                              Credential.Kind kind,
                                              String realm)
        throws ClientException, SubversionException
    {
        return configLib.removeCredential(configDir, kind, realm);
    }

    // ### TODO: There are currently no proper APIs in Subversion for
    //           adding credentials. This method is a placeholder.
    //
    ///**
    // * Store a new credential, or replace an existing credential.
    // * <p>
    // * <b>Note:</b> If the native credentials store is disabled, this
    // *              method will always return <code>null</code>.
    // *
    // * @param configDir The path to the configuration directory; if
    // *        <code>null</code>, the default (system-specific) user
    // *        configuration path will be used.
    // * @param credential The credential to store.
    // * @param replace If <code>true</code>, any existing matching
    // *        credential will be replaced.
    // *
    // * @return the stored credential. If <code>replace</code> was
    // * <code>false</code>, and a credential with the same kind and
    // * for the same realm exists, it will be returned. If the given
    // * credential was successfully added, the same object reference
    // * will be returned (the calling code can compare reference values
    // * to determine this). Will return <code>null</code> if the
    // * credential could not be stored for any reason.
    // */
    //public static Credential addCredential(String configDir,
    //                                       Credential credential,
    //                                       boolean replace)
    //    throws ClientException, SubversionException
    //{
    //    return configLib.addCredential(configDir, credential, replace);
    //}

    /**
     * Find stored credentials that match the given search criteria.
     * <p>
     * <b>Note:</b> If the native credentials store is disabled, this
     *              method will always return <code>null</code>.
     *
     * @param configDir The path to the configuration directory; if
     *        <code>null</code>, the default (system-specific) user
     *        configuration path will be used.
     * @param kind The kind of the credential; if <code>null</code>,
     *             all matching credential types will be returned.
     * @param realmPattern A glob pattern for the realm string;
     *             if <code>null</code>, all realms will be considered;
     *             otherwise, only those credentials whose realm matches
     *             the pattern will be returned.
     * @param usernamePattern A glob pattern for the username;
     *             if <code>null</code>, all credentials will be considered;
     *             otherwise, only those credentials that have a username,
     *             and where the username matches the pattern, will be
     *             returned.
     * @param hostnamePattern A glob pattern for the hostnames of a
     *             server certificate; if <code>null</code>, all
     *             credntials will be considered; otherwise, only
     *             those credentials that have a server certificate
     *             with a hostname that matches the pattern will be
     *             returned.
     * @param textPattern A glob pattern that must match any textual
     *             information in a credential, for example, a realm,
     *             username, certificate details, etc; passwords, passphrases
     *             and other info considered secret will not be matched;
     * @return the list of matching credentials.
     */
    public static List<Credential>
        searchCredentials(String configDir,
                          Credential.Kind kind,
                          String realmPattern,
                          String usernamePattern,
                          String hostnamePattern,
                          String textPattern)
        throws ClientException, SubversionException
    {
        return configLib.searchCredentials(configDir, kind, realmPattern,
                                           usernamePattern, hostnamePattern,
                                           textPattern);
    }

    //
    // Diff and Merge
    //
    private static final DiffLib diffLib = new DiffLib();

    /**
     * Options to control the behaviour of the file diff routines.
     */
    public static class DiffOptions
    {
        /**
         * To what extent whitespace should be ignored when comparing lines.
         */
        public enum IgnoreSpace
        {
            /** Do not ignore whitespace */
            none,

            /**
             * Ignore changes in sequences of whitespace characters,
             * treating each sequence of whitespace characters as a
             * single space.
             */
            change,

            /** Ignore all whitespace characters. */
            all
        }

        /**
         * @param ignoreSpace Whether and how to ignore space differences
         *        in the files. The default is {@link IgnoreSpace#none}.
         * @param ignoreEolStyle Whether to treat all end-of-line
         *        markers the same when comparing lines.  The default
         *        is <code>false</code>.
         * @param showCFunction Whether the "@@" lines of the unified
         *        diff output should include a prefix of the nearest
         *        preceding line that starts with a character that
         *        might be the initial character of a C language
         *        identifier. The default is <code>false</code>.
         */
        public DiffOptions(IgnoreSpace ignoreSpace,
                           boolean ignoreEolStyle,
                           boolean showCFunction)
        {
            this.ignoreSpace = ignoreSpace;
            this.ignoreEolStyle = ignoreEolStyle;
            this.showCFunction = showCFunction;
            this.contextSize = -1;
        }

        /**
         * Like the {@see #DiffOptions(IgnoreSpace,boolean,boolean)},
         * but with an additional parameter.
         * @param contextSize If this is greater than 0, then this
         * number of context lines will be used in the generated diff
         * output. Otherwise the legacy compile time default will be
         * used.
         */
        public DiffOptions(IgnoreSpace ignoreSpace,
                           boolean ignoreEolStyle,
                           boolean showCFunction,
                           int contextSize)
        {
            this.ignoreSpace = ignoreSpace;
            this.ignoreEolStyle = ignoreEolStyle;
            this.showCFunction = showCFunction;
            this.contextSize = contextSize;
        }

        public final IgnoreSpace ignoreSpace;
        public final boolean ignoreEolStyle;
        public final boolean showCFunction;
        public final int contextSize;
    }

    /** Style for displaying conflicts in merge output. */
    public enum ConflictDisplayStyle
    {
        /** Display modified and latest, with conflict markers. */
        modified_latest,

        /**
         * Like <code>modified_latest</code>, but with an extra effort
         * to identify common sequences between modified and latest.
         */
        resolved_modified_latest,

        /** Display modified, original, and latest, with conflict markers. */
        modified_original_latest,

        /** Just display modified, with no markers. */
        modified,

        /** Just display latest, with no markers. */
        latest,

        /**
         * Like <code>modified_original_latest</code>, but
         * <em>only<em> showing conflicts.
         */
        only_conflicts
    }

    /**
     * Given two versions of a file, base (<code>originalFile</code>)
     * and current (<code>modifiedFile</code>), show differences between
     * them in unified diff format.
     *
     * @param originalFile The base file version (unmodified)
     * @param modifiedFile The incoming file version (locally modified)
     * @param diffOptions Options controlling how files are compared.
     *        May be <code>null</code>.
     * @param originalHeader The header to display for the base file
     *        in the unidiff index block. If it is <code>null</code>,
     *        the <code>originalFile</code> path and its modification
     *        time will be used instead.
     * @param modifiedHeader The header to display for the current
     *        file in the unidiff index block. If it is <code>null</code>,
     *        the <code>currentFile</code> path and its modification
     *        time will be used instead.
     * @param headerEncoding The character encoding of the unidiff headers.
     * @param relativeToDir If this parameter is not <null>, it must
     *        be the path of a (possibly non-immediate) parent of both
     *        <code>originalFile</code> and <code>modifiedFile</code>.
     *        This path will be stripped from the beginning of those
     *        file names if they are used in the unidiff index header.
     * @param resultStream The stream that receives the merged output.
     * @return <code>true</code> if there were differences between the files.
     * @throws ClientException
     */
    public static boolean fileDiff(String originalFile,
                                   String modifiedFile,
                                   SVNUtil.DiffOptions diffOptions,

                                   String originalHeader,
                                   String modifiedHeader,
                                   String headerEncoding,
                                   String relativeToDir,

                                   OutputStream resultStream)
        throws ClientException
    {
        // ### TODO: Support cancellation as in svn_diff_file_output_unified3.
        return diffLib.fileDiff(originalFile, modifiedFile, diffOptions,
                                originalHeader, modifiedHeader,
                                headerEncoding,
                                relativeToDir, resultStream);
    }


    /**
     * Given three versions of a file, base (<code>originalFile</code>),
     * incoming (<code>modifiedFile</code>) and current
     * (<code>latestFile</code>, produce a merged result, possibly
     * displaying conflict markers.
     *
     * @param originalFile The base file version (common ancestor)
     * @param modifiedFile The incoming file version (modified elsewhere)
     * @param latestFile The current file version (locally modified)
     * @param diffOptions Options controlling how files are compared.
     *        May be <code>null</code>.
     * @param conflictOriginal Optional custom conflict marker for
     *        the <code>originalFile</code> contents.
     * @param conflictModified Optional custom conflict marker for
     *        the <code>modifiedFile</code> contents.
     * @param conflictLatest Optional custom conflict marker for
     *        the <code>latestFile</code> contents.
     * @param conflictSeparator Optional custom conflict separator.
     * @param conflictStyle Determines how conflicts are displayed.
     * @param resultStream The stream that receives the merged output.
     * @return <code>true</code> if there were any conflicts.
     * @throws ClientException
     */
    public static boolean fileMerge(String originalFile,
                                    String modifiedFile,
                                    String latestFile,
                                    DiffOptions diffOptions,

                                    String conflictOriginal,
                                    String conflictModified,
                                    String conflictLatest,
                                    String conflictSeparator,
                                    ConflictDisplayStyle conflictStyle,

                                    OutputStream resultStream)
        throws ClientException
    {
        return diffLib.fileMerge(originalFile, modifiedFile, latestFile,
                                 diffOptions,
                                 conflictOriginal, conflictModified,
                                 conflictLatest, conflictSeparator,
                                 conflictStyle, resultStream);
    }

    //
    // Property validation and parsing
    //
    private static final PropLib propLib = new PropLib();

    /**
     * Validate the value of an <code>svn:</code> property on file or
     * directory and return a canonical representation of its value.
     * @param name The name of the property (must be a valid svn: property)
     * @param value The property's value
     * @param path The path or URL of the file or directory that
     *        owns the property; only used for error messages
     * @param kind The node kind of the file or dir that owns the property
     * @param mimeType If <code>kind</code> is {@link NodeKind.file}, this is
     *        tye file's mime-type, used for extra validation for the
     *        <code>svn:eol-style</code> property. If it is <code>null</code>,
     *        the extra validation will be skipped.
     * @return a canonicalized representation of the property value
     * @see http://subversion.apache.org/docs/api/latest/group__svn__wc__properties.html#ga83296313ec59cc825176224ac8282ec2
     */
    public static byte[] canonicalizeNodeProperty(
        String name, byte[] value, String path, NodeKind kind,
        String mimeType)
        throws ClientException
    {
        return propLib.canonicalizeNodeProperty(
            name, value, path, kind, mimeType, null);
    }

    /**
     * Validate the value of an <code>svn:</code> property on file or
     * directory and return a canonical representation of its value.
     * @param name The name of the property (must be a valid svn: property)
     * @param value The property's value
     * @param path The path or URL of the file or directory that
     *        owns the property; only used for error messages
     * @param kind The node kind of the file or dir that owns the property
     * @param mimeType If <code>kind</code> is {@link NodeKind.file}, this is
     *        tye file's mime-type, used for extra validation for the
     *        <code>svn:eol-style</code> property. If it is <code>null</code>,
     *        the extra validation will be skipped.
     * @param fileContents A stream with the file's contents. Only used
     *        to check for line-ending consistency when validating the
     *        <code>svn:eol-style</code> property, and only when
     *        <code>kind</code> is {@link NodeKind.file} and
     *        <code>mimeType</code> is not <code>null</code>.
     * @return a canonicalized representation of the property value
     * @see http://subversion.apache.org/docs/api/latest/group__svn__wc__properties.html#ga83296313ec59cc825176224ac8282ec2
     */
    public static byte[] canonicalizeNodeProperty(
        String name, byte[] value, String path, NodeKind kind,
        String mimeType, InputStream fileContents)
        throws ClientException
    {
        return propLib.canonicalizeNodeProperty(
            name, value, path, kind, mimeType, fileContents);
    }

    /**
     * Parse <code>description</code>, assuming it is an externals
     * specification in the format required for the
     * <code>svn:externals</code> property, and return a list of
     * parsed external items.
     * @param description The externals description.
     * @param parentDirectory Used to construct error messages.
     * @param canonicalizeUrl Whe <code>true</code>, canonicalize the
     *     <code>url</code> member of the returned objects. If the
     *     <code>url</code> member refers to an absolute URL, it will
     *     be canonicalized as URL consistent with the way URLs are
     *     canonicalized throughout the Subversion API. If, however,
     *     the <code>url</code> member makes use of the recognized
     *     (SVN-specific) relative URL syntax for
     *     <code>svn:externals</code>, "canonicalization" is an
     *     ill-defined concept which may even result in munging the
     *     relative URL syntax beyond recognition. You've been warned.
     * @return a list of {@link ExternalItem}s
     */
    public static List<ExternalItem> parseExternals(byte[] description,
                                                    String parentDirectory,
                                                    boolean canonicalizeUrl)
        throws ClientException
    {
        return propLib.parseExternals(description, parentDirectory,
                                      canonicalizeUrl);
    }

    /**
     * Unparse and list of external items into a format suitable for
     * the value of the <code>svn:externals</code> property and
     * validate the result.
     * @param items The list of {@link ExternalItem}s
     * @param parentDirectory Used to construct error messages.
     * @param compatibleWithSvn1_5 When <code>true</code>, the format
     *     of the returned property value will be compatible with
     *     clients older than Subversion 1.5.
     */
    public static byte[] unparseExternals(List<ExternalItem> items,
                                          String parentDirectory)
        throws SubversionException
    {
        return propLib.unparseExternals(items, parentDirectory, false);
    }

    /**
     * Unparse and list of external items into a format suitable for
     * the value of the <code>svn:externals</code> property compatible
     * with Subversion clients older than release 1.5, and validate
     * the result.
     * @param items The list of {@link ExternalItem}s
     * @param parentDirectory Used to construct error messages.
     */
    public static byte[] unparseExternalsForAncientUnsupportedClients(
        List<ExternalItem> items, String parentDirectory)
        throws SubversionException
    {
        return propLib.unparseExternals(items, parentDirectory, true);
    }

    /**
     * If the URL in <code>external</code> is relative, resolve it to
     * an absolute URL, using <code>reposRootUrl</code> and
     * <code>parentDirUrl</code> to provide contest.
     *<p>
     * Regardless if the URL is absolute or not, if there are no
     * errors, the returned URL will be canonicalized.
     *<p>
     * The following relative URL formats are supported:
     * <dl>
     *  <dt><code>../</code></dt>
     *  <dd>relative to the parent directory of the external</dd>
     *  <dt><code>^/</code></dt>
     *  <dd>relative to the repository root</dd>
     *  <dt><code>//</code></dt>
     *  <dd>relative to the scheme</dd>
     *  <dt><code>/</code></dt>
     *  <dd>relative to the server's hostname</dd>
     * </dl>
     *<p>
     * The <code>../<code> and ^/ relative URLs may use <code>..</code>
     * to remove path elements up to the server root.
     *<p>
     * The external URL should not be canonicalized before calling
     * this function, as otherwise the scheme relative URL
     * '<code>//host/some/path</code>' would have been canonicalized
     * to '<code>/host/some/path</code>' and we would not be able to
     * match on the leading '<code>//</code>'.
    */
    public static String resolveExternalsUrl(ExternalItem external,
                                             String reposRootUrl,
                                             String parentDirUrl)
        throws ClientException
    {
        return propLib.resolveExternalsUrl(
                   external, reposRootUrl, parentDirUrl);
    }

    //
    // Newline translation and keyword expansion
    //
    private static final SubstLib substLib = new SubstLib();

    /**
     * Use the linefeed code point ('<code>\x0a</code>')
     * for the newline separator.
     * @see translateStream
     * @see untranslateStream
     */
    public static final byte[] EOL_LF = SubstLib.EOL_LF;

    /**
     * Use the carraige-return code point ('<code>\x0d</code>')
     * for the newline separator.
     * @see translateStream
     * @see untranslateStream
     */
    public static final byte[] EOL_CR = SubstLib.EOL_CR;

    /**
     * Use carriage-return/linefeed sequence ('<code>\x0d\x0a</code>')
     * for the newline separator.
     * @see translateStream
     * @see untranslateStream
     */
    public static final byte[] EOL_CRLF = SubstLib.EOL_CRLF;


    /**
     * Build a dictionary of expanded keyword values, given the
     * contents of a file's <code>svn:keywords</code> property, its
     * revision, URL, the date it was committed on, the author of the
     * commit and teh URL of the repository root.
     *<p>
     * Custom keywords defined in <code>svn:keywords</code> properties
     * are expanded using the provided parameters and in accordance
     * with the following format substitutions in the
     * <code>keywordsValue</code>:
     * <dl>
     *   <dt><code>%a</dt></code>
     * <dd>The author.</dd>
     *   <dt><code>%b</dt></code>
     * <dd>The basename of the URL.</dd>
     *   <dt><code>%d</dt></code>
     * <dd>Short format of the date.</dd>
     *   <dt><code>%D</dt></code>
     * <dd>Long format of the date.</dd>
     *   <dt><code>%P</dt></code>
     * <dd>The file's path, relative to the repository root URL.</dd>
     *   <dt><code>%r</dt></code>
     * <dd>The revision.</dd>
     *   <dt><code>%R</dt></code>
     * <dd>The URL to the root of the repository.</dd>
     *   <dt><code>%u</dt></code>
     * <dd>The URL of the file.</dd>
     *   <dt><code>%_</dt></code>
     * <dd>A space (keyword definitions cannot contain a literal space).</dd>
     *   <dt><code>%%</dt></code>
     * <dd>A literal '%'.</dd>
     *   <dt><code>%H</dt></code>
     * <dd>Equivalent to <code>%P%_%r%_%d%_%a</code>.</dd>
     *   <dt><code>%I</dt></code>
     * <dd>Equivalent to <code>%b%_%r%_%d%_%a</code>.</dd>
     * </dl>
     *<p>
     * Custom keywords are defined by appending '=' to the keyword
     * name, followed by a string containing any combination of the
     * format substitutions.
     *<p>
     * Any of the <code>revision</code>, <code>url</code>,
     * <code>reposRootUrl</code>, <code>date</code> and
     * <code>author</code> parameters may be <code>null</code>, or
     * {@link Revision#SVN_INVALID_REVNUM} for <code>revision</code>,
     * to indicate that the information is not present. Each piece of
     * information that is not present expands to the empty string
     * wherever it appears in an expanded keyword value.  (This can
     * result in multiple adjacent spaces in the expansion of a
     * multi-valued keyword such as "<code>Id</code>".)
     */
    public static Map<String, byte[]> buildKeywords(byte[] keywordsValue,
                                                    long revision,
                                                    String url,
                                                    String reposRootUrl,
                                                    Date date,
                                                    String author)
        throws SubversionException, ClientException
    {
        return substLib.buildKeywords(keywordsValue, revision,
                                      url, reposRootUrl, date, author);
    }

    /**
     * Return a stream which performs end-of-line translation and
     * keyword expansion when read from.
     *<p>
     * <b>Important:</b> Make sure you close the returned stream to
     * ensure all data are flushed and cleaned up (this will also
     * close the provided stream and dispose the related netive
     * object).
     *<p>
     * If <code>eolMarker</code> is not <code>null</code>, replace
     * whatever any end-of-line sequences in the input with
     * <code>eolMarker</code>.  If the input has an inconsistent line
     * ending style, then:
     * <ul>
     *   <li>if <code>repairEol</code> is <code>false</code>, then a
     *       subsequent read or other operation on the stream will
     *       generate an error when the inconsistency is detected;</li>
     *   <li>if <code>repaorEol</code> is <code>true</code>, convert any
     *       line ending to <code>eolMarker</code>.<br/>
     *       Recognized line endings are: "<code>\n</code>",
     *       "<code>\r</code>", and "<code>\r\n</code>".</li>
     * </ul>
     *<p>
     * Expand or contract keywords using the contents of
     * <code>keywords</code> as the new values.  If
     * <code>expandKeywords</code> is <code>true</code>, expand
     * contracted keywords and re-expand expanded keywords; otherwise,
     * contract expanded keywords and ignore contracted ones.
     * Keywords not found in the dictionary are ignored (not
     * contracted or expanded).  If the <code>keywords</code> itself
     * is <code>null</code>, keyword substitution will be altogether
     * ignored.
     *<p>
     * Detect only keywords that are no longer than
     * <code>SVN_KEYWORD_MAX_LEN</code> bytes (currently: 255),
     * including the delimiters and the keyword itself.
     *<p>
     * Recommendation: if <code>expandKeywords</code> is
     * <code>false</code>, then you don't care about the keyword
     * values, so just put <code>null</code> values into the
     * <code>keywords</code> dictionary.
     *<p>
     * If the inner stream implements marking and seeking via
     * {@link InputStream#mark} and {@link InputStream#reset}, the
     * translated stream will too.
     *
     * @param source the source (untranslated) stream.
     * @param eolMarker the byte sequence to use as the end-of-line marker;
     *     must be one of {@link #EOL_LF}, {@link #EOL_CR}
     *     or {@link #EOL_CRLF}.
     * @param repairEol flag to repair end-of-lines; see above
     * @param keywords the keyword dictionary; see {@link buildKeywords}
     * @param expandKeywords flag to expand keywords
     */
    public static InputStream translateStream(InputStream source,
                                              byte[] eolMarker,
                                              boolean repairEol,
                                              Map<String, byte[]> keywords,
                                              boolean expandKeywords)
        throws SubversionException, ClientException
    {
        return substLib.translateInputStream(
                    source, eolMarker, repairEol,
                    keywords, true, expandKeywords,
                    null, Revision.SVN_INVALID_REVNUM,
                    null, null, null, null);
    }

    /**
     * Expand keywords and return a stream which performs end-of-line
     * translation and keyword expansion when read from.
     * @see buildKeywords
     * @see translateStream(InputStream,byte[],boolean,Map,boolean)
     */
    public static InputStream translateStream(InputStream source,
                                              byte[] eolMarker,
                                              boolean repairEol,
                                              boolean expandKeywords,
                                              byte[] keywordsValue,
                                              long revision,
                                              String url,
                                              String reposRootUrl,
                                              Date date,
                                              String author)
        throws SubversionException, ClientException
    {
        return substLib.translateInputStream(
                    source, eolMarker, repairEol,
                    null, false, expandKeywords,
                    keywordsValue, revision,
                    url, reposRootUrl, date, author);
    }

    /**
     * Return a stream which performs end-of-line translation and
     * keyword expansion when written to. Behaves like
     * {@link #translateStream(InputStream,byte[],boolean,Map,boolean)},
     * except that it translates an <code>OutputStream</code> and never
     * supports marking and seeking.
     */
    public static OutputStream translateStream(OutputStream destination,
                                               byte[] eolMarker,
                                               boolean repairEol,
                                               Map<String, byte[]> keywords,
                                               boolean expandKeywords)
        throws SubversionException, ClientException
    {
        return substLib.translateOutputStream(
                    destination, eolMarker, repairEol,
                    keywords, true, expandKeywords,
                    null, Revision.SVN_INVALID_REVNUM,
                    null, null, null, null);
    }

    /**
     * Expand keywords and return a stream which performs end-of-line
     * translation and keyword expansion when written to.
     * @see buildKeywords
     * @see translateStream(OutputStream,byte[],boolean,Map,boolean)
     */
    public static OutputStream translateStream(OutputStream destination,
                                               byte[] eolMarker,
                                               boolean repairEol,
                                               boolean expandKeywords,
                                               byte[] keywordsValue,
                                               long revision,
                                               String url,
                                               String reposRootUrl,
                                               Date date,
                                               String author)
        throws SubversionException, ClientException
    {
        return substLib.translateOutputStream(
                    destination, eolMarker, repairEol,
                    null, false, expandKeywords,
                    keywordsValue, revision,
                    url, reposRootUrl, date, author);
    }
}
