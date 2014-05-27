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
     * retrieve the username entered during the prompt call
     * @return the username
     */
    public String getUsername();

    /**
     * retrieve the password entered during the prompt call
     * @return the password
     */
    public String getPassword();


    /**
     * The callback implementation stores the result of each method
     * call in objects of this class.
     */
    public static class Result implements java.io.Serializable
    {
        // Update the serialVersionUID when there is a incompatible change made to
        // this class.  See the java documentation for when a change is incompatible.
        // http://java.sun.com/javase/7/docs/platform/serialization/spec/version.html#6678
        private static final long serialVersionUID = 1L;

        /**
         * Call this method if the interaction with the user was
         * interrupted (e.g., a GUI dialogue was cancelled).
         * The return value of the callback is ignored if this flag is set.
         */
        public void cancel()
        {
            cancelled = true;
        }

        /**
         * Call this method to allow permanently storing the result of
         * the callback in the credentials store.
         */
        public void allowSave()
        {
            save = true;
        }

        /**
         * Call this method to forbid storing the result of the
         * callback in the credentials store. This is the default.
         */
        public void forbidSave()
        {
            save = false;
        }

        private boolean cancelled = false;
        private boolean save = false;
    }

    /**
     * Ask for a username.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, calling result.allowSave()
     *                 will have no effect.
     * @param result   The result of the callback.
     * @return The username for the <code>realm</code>.
     */
    public String usernamePrompt(String realm,
                                 boolean maySave, Result result);

    /**
     * Ask for a password.
     * @param realm    The realm from which the question originates.
     * @param username The username for the realm.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, calling result.allowSave()
     *                 will have no effect.
     * @param result   The result of the callback.
     * @return The password for <code>username</code> in the <code>realm</code>.
     */
    public String passwordPrompt(String realm, String username,
                                 boolean maySave, Result result);


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
            return ((failures & OTHER) != 0);
        }

        private static final int NOT_YET_VALID = 0x00000001;
        private static final int EXPIRED       = 0x00000002;
        private static final int CN_MISMATCH   = 0x00000004;
        private static final int UNKNOWN_CA    = 0x00000008;
        private static final int OTHER         = 0x40000000;

        /* This private constructor is used by the native implementation. */
        private SSLServerCertFailures(int failures)
        {
            /* Double-check that we did not forget to map any of the
               failure flags, and flag an "other" failure. */
            final int missing = failures & ~(NOT_YET_VALID | EXPIRED
                                             | CN_MISMATCH | UNKNOWN_CA
                                             | OTHER);
            if (missing != 0) {
                Logger log = Logger.getLogger("org.apache.subversion.javahl");
                log.warning(String.format("Unknown SSL certificate parsing "
                                          + "failure flags: %1$x", missing));
                failures |= OTHER;
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
         * @return The primary CN of the certificate.
         */
        public String hostname()
        {
            return cn;
        }

        /**
         * @return The text representation of the certificate fingerprint.
         */
        public String fingerprint()
        {
            return fpr;
        }

        /**
         * @return The text represent representation of the date from
         *         which the certificate is valid.
         */
        public String validFrom()
        {
            return startDate;
        }

        /**
         * @return The text represent representation of the date after
         *         which the certificate is no longer valid.
         */
        public String validUntil()
        {
            return endDate;
        }

        /**
         * @return The DN of the certificate issuer.
         */
        public String issuer()
        {
            return dn;
        }

        /**
         * @return the Base64-encoded DER representation of the certificate.
         */
        public String text()
        {
            return der;
        }

        /* This private constructor is used by the native implementation. */
        private SSLServerCertInfo(String cn, String fpr,
                                  String startDate, String endDate,
                                  String dn, String der)
        {
            this.cn = cn;
            this.fpr = fpr;
            this.startDate = startDate;
            this.endDate = endDate;
            this.dn = dn;
            this.der = der;
        }

        private String cn;
        private String fpr;
        private String startDate;
        private String endDate;
        private String dn;
        private String der;
    }

    /**
     * Ask if we trust the server certificate.
     * @param realm    The realm from which the question originates.
     * @param failures The result of parsing the certificate;
     *                 if <code>null</code>, there were no failures..
     * @param info     Information extracted from the certificate.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, calling result.allowSave()
     *                 will have no effect.
     * @param result   The result of the callback.
     * @return <code>false</code> to reject server certificate; otherwise,
     *         {@see Result#forbidSave()} indicates that the cert should be
     *         accepted for only one operation.
     */
    public boolean sslServerTrustPrompt(String realm,
                                        SSLServerCertFailures failures,
                                        SSLServerCertInfo info,
                                        boolean maySave, Result result);

    /**
     * Ask for the (local) file name of a client SSL certificate.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, calling result.allowSave()
     *                 will have no effect.
     * @param result   The result of the callback.
     * @return The file name of a client certificate for <code>realm</code>.
     */
    public String sslClientCertPrompt(String realm, boolean maySave,
                                      Result result);

    /**
     * Ask for passphrase for decrypting a client SSL certificate.
     * @param realm    The realm from which the question originates.
     * @param maySave  Indiceates whether saving credentials is allowed;
     *                 if <code>false</code>, calling result.allowSave()
     *                 will have no effect.
     * @param result   The result of the callback.
     * @return The the passphrase for the client certificate.
     */
    public String sslClientCertPassphrasePrompt(String realm, boolean maySave,
                                                Result result);

    /**
     * Ask if a password may be stored on disk in plaintext.
     * @param realm    The realm from which the question originates.
     * @return <code>true</code> if the password may be stored in plaintext.
     */
    public boolean storePlaintextPasswordPrompt(String realm);

    /**
     * Ask if a certificate passphrase may be stored on disk in plaintext.
     * @param realm    The realm from which the question originates.
     * @return <code>true</code> if the passphrase may be stored in plaintext.
     */
    public boolean storePlaintextPassphrasePrompt(String realm);
}
