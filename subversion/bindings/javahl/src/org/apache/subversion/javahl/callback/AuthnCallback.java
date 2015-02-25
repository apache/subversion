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

package org.apache.subversion.javahl.callback;

import java.util.Date;
import java.util.List;
import java.util.logging.Logger;

/**
 * <p>The interface for requesting authentication credentials from the
 * user.  Should the javahl bindings need the matching information,
 * these methodes will be called.</p>
 *
 * <p>This callback can also be used to provide the equivalent of the
 * <code>--no-auth-cache</code> and <code>--non-interactive</code>
 * arguments accepted by the command-line client.</p>
 *
 * @since 1.9
 */
public interface AuthnCallback
{
    /**
     * Abstract base class for callback results.
     */
    public abstract class AuthnResult
    {
        protected boolean save = false;   // Allow saving the credentials
        protected boolean trust = false;  // SSL server cert trust
        protected String identity = null; // Username or client cert filename
        protected String secret = null;   // Password or client cert passphrase
    }

    /**
     * The result type used by {@see #usernamePrompt}.
     */
    public static final class UsernameResult
        extends AuthnResult
        implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Set the username in the result.
         * Assumes the result may not be stored permanently.
         * @param username The username.
         */
        public UsernameResult(String username)
        {
            identity = username;
        }

        /**
         * Set the username in the result.
         * @param username The username.
         * @param maySave Set if the result may be stored permanently.
         */
        public UsernameResult(String username, boolean maySave)
        {
            save = maySave;
            identity = username;
        }
    }

    /**
     * Ask for a username.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, the <code>maySave</code> flag
     *                 in the return value will be ignored.
     * @return The result, or <code>null</code> if cancelled.
     */
    public UsernameResult usernamePrompt(String realm, boolean maySave);


    /**
     * The result type used by {@see #userPasswordPrompt}.
     */
    public static final class UserPasswordResult
        extends AuthnResult
        implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Set the username and password in the result.
         * Assumes the result may not be stored permanently.
         * @param username The username.
         * @param password The password.
         */
        public UserPasswordResult(String username, String password)
        {
            identity = username;
            secret = password;
        }

        /**
         * Set the username and password in the result.
         * @param username The user name.
         * @param password The password.
         * @param maySave Set if the result may be stored permanently.
         */
        public UserPasswordResult(String username, String password,
                                  boolean maySave)
        {
            save = maySave;
            identity = username;
            secret = password;
        }
    }

    /**
     * Ask for a username and password.
     * @param realm    The realm from which the question originates.
     * @param username The username for the realm, if known; may be <code>null</code>.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, the <code>maySave</code> flag
     *                 in the return value will be ignored.
     * @return The result, or <code>null</code> if cancelled.
     */
    public UserPasswordResult userPasswordPrompt(String realm, String username,
                                                 boolean maySave);


    /**
     * Information about why parsing a server SSL certificate failed.
     */
    public static class SSLServerCertFailures implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * The certificate is not yet valid.
         */
        public boolean notYetValid()
        {
            return ((failures & NOT_YET_VALID) != 0);
        }

        /**
         * The certificate has expired.
         */
        public boolean expired()
        {
            return ((failures & EXPIRED) != 0);
        }

        /**
         * Certificate's CN (hostname) does not match the remote hostname.
         */
        public boolean cnMismatch()
        {
            return ((failures & CN_MISMATCH) != 0);
        }

        /**
         * Certificate authority is unknown (i.e., not trusted).
         */
        public boolean unknownCA()
        {
            return ((failures & UNKNOWN_CA) != 0);
        }

        /**
         * Other failure. This can happen if an unknown failure occurs
         * that we do not handle yet.
         */
        public boolean other()
        {
            return ((failures & OTHER) != 0 || (failures & ~ALL_KNOWN) != 0);
        }

        /** @return the internal bitfield representation of the failures. */
        public int getFailures()
        {
            return failures;
        }

        private static final int NOT_YET_VALID = 0x00000001;
        private static final int EXPIRED       = 0x00000002;
        private static final int CN_MISMATCH   = 0x00000004;
        private static final int UNKNOWN_CA    = 0x00000008;
        private static final int OTHER         = 0x40000000;

        private static final int ALL_KNOWN     = (NOT_YET_VALID | EXPIRED
                                                  | CN_MISMATCH | UNKNOWN_CA
                                                  | OTHER);

        /* This private constructor is used by the native implementation. */
        private SSLServerCertFailures(int failures)
        {
            /* Double-check that we did not forget to map any of the
               failure flags, and flag an "other" failure. */
            final int missing = (failures & ~ALL_KNOWN);

            if (missing != 0) {
                Logger log = Logger.getLogger("org.apache.subversion.javahl");
                log.warning(String.format("Unknown SSL certificate parsing "
                                          + "failure flags: %1$x", missing));
            }

            this.failures = failures;
        }

        private int failures;
    }

    /**
     * Detailed information about the parsed server SSL certificate.
     */
    public static class SSLServerCertInfo implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * @return The subject of the certificate.
         */
        public String getSubject()
        {
            return subject;
        }

        /**
         * @return The certificate issuer.
         */
        public String getIssuer()
        {
            return issuer;
        }

        /**
         * @return The from which the certificate is valid.
         */
        public Date getValidFrom()
        {
            return validFrom;
        }

        /**
         * @return The date after which the certificate is no longer valid.
         */
        public Date getValidTo()
        {
            return validTo;
        }

        /**
         * @return The certificate fingerprint.
         */
        public byte[] getFingerprint()
        {
            return fingerprint;
        }

        /**
         * @return A list of host names that the certificate represents.
         */
        public List<String> getHostnames()
        {
            return hostnames;
        }

        /**
         * @return the Base64-encoded raw certificate data.
         */
        public String getCert()
        {
            return asciiCert;
        }

        /* This private constructor is used by the native implementation. */
        private SSLServerCertInfo(String subject, String issuer,
                                  long validFrom, long validTo,
                                  byte[] fingerprint,
                                  List<String> hostnames,
                                  String asciiCert)
        {
            this.subject = subject;
            this.issuer = issuer;
            this.validFrom = new Date(validFrom);
            this.validTo = new Date(validTo);
            this.fingerprint = fingerprint;
            this.hostnames = hostnames;
            this.asciiCert = asciiCert;
        }

        private String subject;
        private String issuer;
        private Date validFrom;
        private Date validTo;
        private byte[] fingerprint;
        private List<String> hostnames;
        private String asciiCert;
    }

    /**
     * The result type used by {@see #sslServerTrustPrompt}.
     */
    public static final class SSLServerTrustResult
        extends AuthnResult
        implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Create a result that rejects the certificate.
         */
        public static SSLServerTrustResult reject()
        {
            return new SSLServerTrustResult(false, false);
        }

        /**
         * Create a result that temporarily accepts the certificate,
         * for the duration of the current connection.
         */
        public static SSLServerTrustResult acceptTemporarily()
        {
            return new SSLServerTrustResult(true, false);
        }

        /**
         * Create a result that permanently accepts the certificate.
         */
        public static SSLServerTrustResult acceptPermanently()
        {
            return new SSLServerTrustResult(true, true);
        }

        private SSLServerTrustResult(boolean accept, boolean maySave)
        {
            save = maySave;
            trust = accept;
        }
    }

    /**
     * Ask if we trust the server certificate.
     * @param realm    The realm from which the question originates.
     * @param failures The result of parsing the certificate;
     *                 if <code>null</code>, there were no failures.
     * @param info     Information extracted from the certificate.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, the <code>maySave</code> flag
     *                 in the return value will be ignored.
     * @return The result, or <code>null</code> if cancelled.
     */
    public SSLServerTrustResult
        sslServerTrustPrompt(String realm,
                             SSLServerCertFailures failures,
                             SSLServerCertInfo info,
                             boolean maySave);


    /**
     * The result type used by {@see #sslClientCertPrompt}.
     */
    public static final class SSLClientCertResult
        extends AuthnResult
        implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Set the absolute path of the cerfiticate file in the result.
         * Assumes the result may not be stored permanently.
         * @param path The absolute path of the certificate.
         */
        public SSLClientCertResult(String path)
        {
            identity = path;
        }

        /**
         * Set the absolute path of the cerfiticate file in the result.
         * @param path The absolute path of the certificate.
         * @param maySave Set if the result may be stored permanently.
         */
        public SSLClientCertResult(String path, boolean maySave)
        {
            save = maySave;
            identity = path;
        }
    }

    /**
     * Ask for the (local) file name of a client SSL certificate.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, the <code>maySave</code> flag
     *                 in the return value will be ignored.
     * @return The result, or <code>null</code> if cancelled.
     */
    public SSLClientCertResult
        sslClientCertPrompt(String realm, boolean maySave);


    /**
     * The result type used by {@see #sslClientCertPassphrasePrompt}.
     */
    public static final class SSLClientCertPassphraseResult
        extends AuthnResult
        implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Set the cerfiticate passphrase in the result.
         * Assumes the result may not be stored permanently.
         * @param passphrase The passphrase for decrypting the certificate.
         */
        public SSLClientCertPassphraseResult(String passphrase)
        {
            secret = passphrase;
        }

        /**
         * Set the cerfiticate passphrase in the result.
         * @param passphrase The passphrase for decrypting the certificate.
         * @param maySave Set if the result may be stored permanently.
         */
        public SSLClientCertPassphraseResult(String passphrase, boolean maySave)
        {
            save = maySave;
            secret = passphrase;
        }
    }

    /**
     * Ask for passphrase for decrypting a client SSL certificate.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, the <code>maySave</code> flag
     *                 in the return value will be ignored.
     * @return The result, or <code>null</code> if cancelled.
     */
    public SSLClientCertPassphraseResult
        sslClientCertPassphrasePrompt(String realm, boolean maySave);


    /**
     * Ask if a password may be stored on disk in plaintext.
     * @param realm    The realm from which the question originates.
     * @return <code>true</code> if the password may be stored in plaintext.
     */
    public boolean allowStorePlaintextPassword(String realm);

    /**
     * Ask if a certificate passphrase may be stored on disk in plaintext.
     * @param realm    The realm from which the question originates.
     * @return <code>true</code> if the passphrase may be stored in plaintext.
     */
    public boolean allowStorePlaintextPassphrase(String realm);
}
