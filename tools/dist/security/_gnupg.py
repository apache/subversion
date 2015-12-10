""" A wrapper for the 'gpg' command::

Portions of this module are derived from A.M. Kuchling's well-designed
GPG.py, using Richard Jones' updated version 1.3, which can be found
in the pycrypto CVS repository on Sourceforge:

http://pycrypto.cvs.sourceforge.net/viewvc/pycrypto/gpg/GPG.py

This module is *not* forward-compatible with amk's; some of the
old interface has changed.  For instance, since I've added decrypt
functionality, I elected to initialize with a 'gnupghome' argument
instead of 'keyring', so that gpg can find both the public and secret
keyrings.  I've also altered some of the returned objects in order for
the caller to not have to know as much about the internals of the
result classes.

While the rest of ISconf is released under the GPL, I am releasing
this single file under the same terms that A.M. Kuchling used for
pycrypto.

Steve Traugott, stevegt@terraluna.org
Thu Jun 23 21:27:20 PDT 2005

This version of the module has been modified from Steve Traugott's version
(see http://trac.t7a.org/isconf/browser/trunk/lib/python/isconf/GPG.py) by
Vinay Sajip to make use of the subprocess module (Steve's version uses os.fork()
and so does not work on Windows). Renamed to gnupg.py to avoid confusion with
the previous versions.

Modifications Copyright (C) 2008-2014 Vinay Sajip. All rights reserved.

A unittest harness (test_gnupg.py) has also been added.
"""

__version__ = "0.3.8.dev0"
__author__ = "Vinay Sajip"
__date__  = "$07-Dec-2014 18:46:17$"

try:
    from io import StringIO
except ImportError:
    from cStringIO import StringIO

import codecs
import locale
import logging
import os
import re
import socket
from subprocess import Popen
from subprocess import PIPE
import sys
import threading

STARTUPINFO = None
if os.name == 'nt':
    try:
        from subprocess import STARTUPINFO, STARTF_USESHOWWINDOW, SW_HIDE
    except ImportError:
        STARTUPINFO = None

try:
    import logging.NullHandler as NullHandler
except ImportError:
    class NullHandler(logging.Handler):
        def handle(self, record):
            pass
try:
    unicode
    _py3k = False
    string_types = basestring
    text_type = unicode
except NameError:
    _py3k = True
    string_types = str
    text_type = str

logger = logging.getLogger(__name__)
if not logger.handlers:
    logger.addHandler(NullHandler())

# We use the test below because it works for Jython as well as CPython
if os.path.__name__ == 'ntpath':
    # On Windows, we don't need shell quoting, other than worrying about
    # paths with spaces in them.
    def shell_quote(s):
        return '"%s"' % s
else:
    # Section copied from sarge

    # This regex determines which shell input needs quoting
    # because it may be unsafe
    UNSAFE = re.compile(r'[^\w%+,./:=@-]')

    def shell_quote(s):
        """
        Quote text so that it is safe for Posix command shells.

        For example, "*.py" would be converted to "'*.py'". If the text is
        considered safe it is returned unquoted.

        :param s: The value to quote
        :type s: str (or unicode on 2.x)
        :return: A safe version of the input, from the point of view of Posix
                 command shells
        :rtype: The passed-in type
        """
        if not isinstance(s, string_types):
            raise TypeError('Expected string type, got %s' % type(s))
        if not s:
            result = "''"
        elif not UNSAFE.search(s):
            result = s
        else:
            result = "'%s'" % s.replace("'", r"'\''")
        return result

    # end of sarge code

# Now that we use shell=False, we shouldn't need to quote arguments.
# Use no_quote instead of shell_quote to remind us of where quoting
# was needed.
def no_quote(s):
    return s

def _copy_data(instream, outstream):
    # Copy one stream to another
    sent = 0
    if hasattr(sys.stdin, 'encoding'):
        enc = sys.stdin.encoding
    else:
        enc = 'ascii'
    while True:
        data = instream.read(1024)
        if not data:
            break
        sent += len(data)
        logger.debug("sending chunk (%d): %r", sent, data[:256])
        try:
            outstream.write(data)
        except UnicodeError:
            outstream.write(data.encode(enc))
        except:
            # Can sometimes get 'broken pipe' errors even when the data has all
            # been sent
            logger.exception('Error sending data')
            break
    try:
        outstream.close()
    except IOError:
        logger.warning('Exception occurred while closing: ignored', exc_info=1)
    logger.debug("closed output, %d bytes sent", sent)

def _threaded_copy_data(instream, outstream):
    wr = threading.Thread(target=_copy_data, args=(instream, outstream))
    wr.setDaemon(True)
    logger.debug('data copier: %r, %r, %r', wr, instream, outstream)
    wr.start()
    return wr

def _write_passphrase(stream, passphrase, encoding):
    passphrase = '%s\n' % passphrase
    passphrase = passphrase.encode(encoding)
    stream.write(passphrase)
    logger.debug('Wrote passphrase')

def _is_sequence(instance):
    return isinstance(instance, (list, tuple, set, frozenset))

def _make_memory_stream(s):
    try:
        from io import BytesIO
        rv = BytesIO(s)
    except ImportError:
        rv = StringIO(s)
    return rv

def _make_binary_stream(s, encoding):
    if _py3k:
        if isinstance(s, str):
            s = s.encode(encoding)
    else:
        if type(s) is not str:
            s = s.encode(encoding)
    return _make_memory_stream(s)

class Verify(object):
    "Handle status messages for --verify"

    TRUST_UNDEFINED = 0
    TRUST_NEVER = 1
    TRUST_MARGINAL = 2
    TRUST_FULLY = 3
    TRUST_ULTIMATE = 4

    TRUST_LEVELS = {
        "TRUST_UNDEFINED" : TRUST_UNDEFINED,
        "TRUST_NEVER" : TRUST_NEVER,
        "TRUST_MARGINAL" : TRUST_MARGINAL,
        "TRUST_FULLY" : TRUST_FULLY,
        "TRUST_ULTIMATE" : TRUST_ULTIMATE,
    }

    def __init__(self, gpg):
        self.gpg = gpg
        self.valid = False
        self.fingerprint = self.creation_date = self.timestamp = None
        self.signature_id = self.key_id = None
        self.username = None
        self.key_status = None
        self.status = None
        self.pubkey_fingerprint = None
        self.expire_timestamp = None
        self.sig_timestamp = None
        self.trust_text = None
        self.trust_level = None

    def __nonzero__(self):
        return self.valid

    __bool__ = __nonzero__

    def handle_status(self, key, value):
        if key in self.TRUST_LEVELS:
            self.trust_text = key
            self.trust_level = self.TRUST_LEVELS[key]
        elif key in ("RSA_OR_IDEA", "NODATA", "IMPORT_RES", "PLAINTEXT",
                     "PLAINTEXT_LENGTH", "POLICY_URL", "DECRYPTION_INFO",
                     "DECRYPTION_OKAY", "INV_SGNR", "FILE_START", "FILE_ERROR",
                     "FILE_DONE", "PKA_TRUST_GOOD", "PKA_TRUST_BAD", "BADMDC",
                     "GOODMDC", "NO_SGNR", "NOTATION_NAME", "NOTATION_DATA",
                     "PROGRESS", "PINENTRY_LAUNCHED", "NEWSIG"):
            pass
        elif key == "BADSIG":
            self.valid = False
            self.status = 'signature bad'
            self.key_id, self.username = value.split(None, 1)
        elif key == "ERRSIG":
            self.valid = False
            (self.key_id,
             algo, hash_algo,
             cls,
             self.timestamp) = value.split()[:5]
            self.status = 'signature error'
        elif key == "EXPSIG":
            self.valid = False
            self.status = 'signature expired'
            self.key_id, self.username = value.split(None, 1)
        elif key == "GOODSIG":
            self.valid = True
            self.status = 'signature good'
            self.key_id, self.username = value.split(None, 1)
        elif key == "VALIDSIG":
            (self.fingerprint,
             self.creation_date,
             self.sig_timestamp,
             self.expire_timestamp) = value.split()[:4]
            # may be different if signature is made with a subkey
            self.pubkey_fingerprint = value.split()[-1]
            self.status = 'signature valid'
        elif key == "SIG_ID":
            (self.signature_id,
             self.creation_date, self.timestamp) = value.split()
        elif key == "DECRYPTION_FAILED":
            self.valid = False
            self.key_id = value
            self.status = 'decryption failed'
        elif key == "NO_PUBKEY":
            self.valid = False
            self.key_id = value
            self.status = 'no public key'
        elif key in ("KEYEXPIRED", "SIGEXPIRED", "KEYREVOKED"):
            # these are useless in verify, since they are spit out for any
            # pub/subkeys on the key, not just the one doing the signing.
            # if we want to check for signatures with expired key,
            # the relevant flag is EXPKEYSIG or REVKEYSIG.
            pass
        elif key in ("EXPKEYSIG", "REVKEYSIG"):
            # signed with expired or revoked key
            self.valid = False
            self.key_id = value.split()[0]
            if key == "EXPKEYSIG":
                self.key_status = 'signing key has expired'
            else:
                self.key_status = 'signing key was revoked'
            self.status = self.key_status
        elif key == "UNEXPECTED":
            self.valid = False
            self.key_id = value
            self.status = 'unexpected data'
        else:
            raise ValueError("Unknown status message: %r" % key)

class ImportResult(object):
    "Handle status messages for --import"

    counts = '''count no_user_id imported imported_rsa unchanged
            n_uids n_subk n_sigs n_revoc sec_read sec_imported
            sec_dups not_imported'''.split()
    def __init__(self, gpg):
        self.gpg = gpg
        self.imported = []
        self.results = []
        self.fingerprints = []
        for result in self.counts:
            setattr(self, result, None)

    def __nonzero__(self):
        if self.not_imported: return False
        if not self.fingerprints: return False
        return True

    __bool__ = __nonzero__

    ok_reason = {
        '0': 'Not actually changed',
        '1': 'Entirely new key',
        '2': 'New user IDs',
        '4': 'New signatures',
        '8': 'New subkeys',
        '16': 'Contains private key',
    }

    problem_reason = {
        '0': 'No specific reason given',
        '1': 'Invalid Certificate',
        '2': 'Issuer Certificate missing',
        '3': 'Certificate Chain too long',
        '4': 'Error storing certificate',
    }

    def handle_status(self, key, value):
        if key == "IMPORTED":
            # this duplicates info we already see in import_ok & import_problem
            pass
        elif key == "NODATA":
            self.results.append({'fingerprint': None,
                'problem': '0', 'text': 'No valid data found'})
        elif key == "IMPORT_OK":
            reason, fingerprint = value.split()
            reasons = []
            for code, text in list(self.ok_reason.items()):
                if int(reason) | int(code) == int(reason):
                    reasons.append(text)
            reasontext = '\n'.join(reasons) + "\n"
            self.results.append({'fingerprint': fingerprint,
                'ok': reason, 'text': reasontext})
            self.fingerprints.append(fingerprint)
        elif key == "IMPORT_PROBLEM":
            try:
                reason, fingerprint = value.split()
            except:
                reason = value
                fingerprint = '<unknown>'
            self.results.append({'fingerprint': fingerprint,
                'problem': reason, 'text': self.problem_reason[reason]})
        elif key == "IMPORT_RES":
            import_res = value.split()
            for i, count in enumerate(self.counts):
                setattr(self, count, int(import_res[i]))
        elif key == "KEYEXPIRED":
            self.results.append({'fingerprint': None,
                'problem': '0', 'text': 'Key expired'})
        elif key == "SIGEXPIRED":
            self.results.append({'fingerprint': None,
                'problem': '0', 'text': 'Signature expired'})
        else:
            raise ValueError("Unknown status message: %r" % key)

    def summary(self):
        l = []
        l.append('%d imported' % self.imported)
        if self.not_imported:
            l.append('%d not imported' % self.not_imported)
        return ', '.join(l)

ESCAPE_PATTERN = re.compile(r'\\x([0-9a-f][0-9a-f])', re.I)
BASIC_ESCAPES = {
    r'\n': '\n',
    r'\r': '\r',
    r'\f': '\f',
    r'\v': '\v',
    r'\b': '\b',
    r'\0': '\0',
}

class SendResult(object):
    def __init__(self, gpg):
        self.gpg = gpg

    def handle_status(self, key, value):
        logger.debug('SendResult: %s: %s', key, value)

class SearchKeys(list):
    ''' Handle status messages for --search-keys.

        Handle pub and uid (relating the latter to the former).

        Don't care about the rest
    '''

    UID_INDEX = 1
    FIELDS = 'type keyid algo length date expires'.split()

    def __init__(self, gpg):
        self.gpg = gpg
        self.curkey = None
        self.fingerprints = []
        self.uids = []

    def get_fields(self, args):
        result = {}
        for i, var in enumerate(self.FIELDS):
            result[var] = args[i]
        result['uids'] = []
        return result

    def pub(self, args):
        self.curkey = curkey = self.get_fields(args)
        self.append(curkey)

    def uid(self, args):
        uid = args[self.UID_INDEX]
        uid = ESCAPE_PATTERN.sub(lambda m: chr(int(m.group(1), 16)), uid)
        for k, v in BASIC_ESCAPES.items():
            uid = uid.replace(k, v)
        self.curkey['uids'].append(uid)
        self.uids.append(uid)

    def handle_status(self, key, value):
        pass

class ListKeys(SearchKeys):
    ''' Handle status messages for --list-keys.

        Handle pub and uid (relating the latter to the former).

        Don't care about (info from src/DETAILS):

        crt = X.509 certificate
        crs = X.509 certificate and private key available
        ssb = secret subkey (secondary key)
        uat = user attribute (same as user id except for field 10).
        sig = signature
        rev = revocation signature
        pkd = public key data (special field format, see below)
        grp = reserved for gpgsm
        rvk = revocation key
    '''

    UID_INDEX = 9
    FIELDS = 'type trust length algo keyid date expires dummy ownertrust uid'.split()

    def key(self, args):
        self.curkey = curkey = self.get_fields(args)
        if curkey['uid']:
            curkey['uids'].append(curkey['uid'])
        del curkey['uid']
        curkey['subkeys'] = []
        self.append(curkey)

    pub = sec = key

    def fpr(self, args):
        self.curkey['fingerprint'] = args[9]
        self.fingerprints.append(args[9])

    def sub(self, args):
        subkey = [args[4], args[11]]
        self.curkey['subkeys'].append(subkey)


class ScanKeys(ListKeys):
    ''' Handle status messages for --with-fingerprint.'''

    def sub(self, args):
        # --with-fingerprint --with-colons somehow outputs fewer colons,
        # use the last value args[-1] instead of args[11]
        subkey = [args[4], args[-1]]
        self.curkey['subkeys'].append(subkey)

class TextHandler(object):
    def _as_text(self):
        return self.data.decode(self.gpg.encoding, self.gpg.decode_errors)

    if _py3k:
        __str__ = _as_text
    else:
        __unicode__ = _as_text

        def __str__(self):
            return self.data


class Crypt(Verify, TextHandler):
    "Handle status messages for --encrypt and --decrypt"
    def __init__(self, gpg):
        Verify.__init__(self, gpg)
        self.data = ''
        self.ok = False
        self.status = ''

    def __nonzero__(self):
        if self.ok: return True
        return False

    __bool__ = __nonzero__

    def handle_status(self, key, value):
        if key in ("ENC_TO", "USERID_HINT", "GOODMDC", "END_DECRYPTION",
                   "BEGIN_SIGNING", "NO_SECKEY", "ERROR", "NODATA", "PROGRESS",
                   "CARDCTRL", "BADMDC", "SC_OP_FAILURE", "SC_OP_SUCCESS",
                   "PINENTRY_LAUNCHED"):
            # in the case of ERROR, this is because a more specific error
            # message will have come first
            pass
        elif key in ("NEED_PASSPHRASE", "BAD_PASSPHRASE", "GOOD_PASSPHRASE",
                     "MISSING_PASSPHRASE", "DECRYPTION_FAILED",
                     "KEY_NOT_CREATED", "NEED_PASSPHRASE_PIN"):
            self.status = key.replace("_", " ").lower()
        elif key == "NEED_PASSPHRASE_SYM":
            self.status = 'need symmetric passphrase'
        elif key == "BEGIN_DECRYPTION":
            self.status = 'decryption incomplete'
        elif key == "BEGIN_ENCRYPTION":
            self.status = 'encryption incomplete'
        elif key == "DECRYPTION_OKAY":
            self.status = 'decryption ok'
            self.ok = True
        elif key == "END_ENCRYPTION":
            self.status = 'encryption ok'
            self.ok = True
        elif key == "INV_RECP":
            self.status = 'invalid recipient'
        elif key == "KEYEXPIRED":
            self.status = 'key expired'
        elif key == "SIG_CREATED":
            self.status = 'sig created'
        elif key == "SIGEXPIRED":
            self.status = 'sig expired'
        else:
            Verify.handle_status(self, key, value)

class GenKey(object):
    "Handle status messages for --gen-key"
    def __init__(self, gpg):
        self.gpg = gpg
        self.type = None
        self.fingerprint = None

    def __nonzero__(self):
        if self.fingerprint: return True
        return False

    __bool__ = __nonzero__

    def __str__(self):
        return self.fingerprint or ''

    def handle_status(self, key, value):
        if key in ("PROGRESS", "GOOD_PASSPHRASE", "NODATA", "KEY_NOT_CREATED",
                   "PINENTRY_LAUNCHED"):
            pass
        elif key == "KEY_CREATED":
            (self.type,self.fingerprint) = value.split()
        else:
            raise ValueError("Unknown status message: %r" % key)

class DeleteResult(object):
    "Handle status messages for --delete-key and --delete-secret-key"
    def __init__(self, gpg):
        self.gpg = gpg
        self.status = 'ok'

    def __str__(self):
        return self.status

    problem_reason = {
        '1': 'No such key',
        '2': 'Must delete secret key first',
        '3': 'Ambiguous specification',
    }

    def handle_status(self, key, value):
        if key == "DELETE_PROBLEM":
            self.status = self.problem_reason.get(value,
                                                  "Unknown error: %r" % value)
        else:
            raise ValueError("Unknown status message: %r" % key)

    def __nonzero__(self):
        return self.status == 'ok'

    __bool__ = __nonzero__


class Sign(TextHandler):
    "Handle status messages for --sign"
    def __init__(self, gpg):
        self.gpg = gpg
        self.type = None
        self.hash_algo = None
        self.fingerprint = None

    def __nonzero__(self):
        return self.fingerprint is not None

    __bool__ = __nonzero__

    def handle_status(self, key, value):
        if key in ("USERID_HINT", "NEED_PASSPHRASE", "BAD_PASSPHRASE",
                   "GOOD_PASSPHRASE", "BEGIN_SIGNING", "CARDCTRL", "INV_SGNR",
                   "NO_SGNR", "MISSING_PASSPHRASE", "NEED_PASSPHRASE_PIN",
                   "SC_OP_FAILURE", "SC_OP_SUCCESS", "PROGRESS"):
            pass
        elif key in ("KEYEXPIRED", "SIGEXPIRED"):
            self.status = 'key expired'
        elif key == "KEYREVOKED":
            self.status = 'key revoked'
        elif key == "SIG_CREATED":
            (self.type,
             algo, self.hash_algo, cls,
             self.timestamp, self.fingerprint
             ) = value.split()
        else:
            raise ValueError("Unknown status message: %r" % key)

VERSION_RE = re.compile(r'gpg \(GnuPG\) (\d+(\.\d+)*)'.encode('ascii'), re.I)
HEX_DIGITS_RE = re.compile(r'[0-9a-f]+$', re.I)

class GPG(object):

    decode_errors = 'strict'

    result_map = {
        'crypt': Crypt,
        'delete': DeleteResult,
        'generate': GenKey,
        'import': ImportResult,
        'send': SendResult,
        'list': ListKeys,
        'scan': ScanKeys,
        'search': SearchKeys,
        'sign': Sign,
        'verify': Verify,
    }

    "Encapsulate access to the gpg executable"
    def __init__(self, gpgbinary='gpg', gnupghome=None, verbose=False,
                 use_agent=False, keyring=None, options=None,
                 secret_keyring=None):
        """Initialize a GPG process wrapper.  Options are:

        gpgbinary -- full pathname for GPG binary.

        gnupghome -- full pathname to where we can find the public and
        private keyrings.  Default is whatever gpg defaults to.
        keyring -- name of alternative keyring file to use, or list of such
        keyrings. If specified, the default keyring is not used.
        options =-- a list of additional options to pass to the GPG binary.
        secret_keyring -- name of alternative secret keyring file to use, or
        list of such keyrings.
        """
        self.gpgbinary = gpgbinary
        self.gnupghome = gnupghome
        if keyring:
            # Allow passing a string or another iterable. Make it uniformly
            # a list of keyring filenames
            if isinstance(keyring, string_types):
                keyring = [keyring]
        self.keyring = keyring
        if secret_keyring:
            # Allow passing a string or another iterable. Make it uniformly
            # a list of keyring filenames
            if isinstance(secret_keyring, string_types):
                secret_keyring = [secret_keyring]
        self.secret_keyring = secret_keyring
        self.verbose = verbose
        self.use_agent = use_agent
        if isinstance(options, str):
            options = [options]
        self.options = options
        # Changed in 0.3.7 to use Latin-1 encoding rather than
        # locale.getpreferredencoding falling back to sys.stdin.encoding
        # falling back to utf-8, because gpg itself uses latin-1 as the default
        # encoding.
        self.encoding = 'latin-1'
        if gnupghome and not os.path.isdir(self.gnupghome):
            os.makedirs(self.gnupghome,0x1C0)
        p = self._open_subprocess(["--version"])
        result = self.result_map['verify'](self) # any result will do for this
        self._collect_output(p, result, stdin=p.stdin)
        if p.returncode != 0:
            raise ValueError("Error invoking gpg: %s: %s" % (p.returncode,
                                                             result.stderr))
        m = VERSION_RE.match(result.data)
        if not m:
            self.version = None
        else:
            dot = '.'.encode('ascii')
            self.version = tuple([int(s) for s in m.groups()[0].split(dot)])

    def make_args(self, args, passphrase):
        """
        Make a list of command line elements for GPG. The value of ``args``
        will be appended. The ``passphrase`` argument needs to be True if
        a passphrase will be sent to GPG, else False.
        """
        cmd = [self.gpgbinary, '--status-fd', '2', '--no-tty']
        if self.gnupghome:
            cmd.extend(['--homedir',  no_quote(self.gnupghome)])
        if self.keyring:
            cmd.append('--no-default-keyring')
            for fn in self.keyring:
                cmd.extend(['--keyring', no_quote(fn)])
        if self.secret_keyring:
            for fn in self.secret_keyring:
                cmd.extend(['--secret-keyring', no_quote(fn)])
        if passphrase:
            cmd.extend(['--batch', '--passphrase-fd', '0'])
        if self.use_agent:
            cmd.append('--use-agent')
        if self.options:
            cmd.extend(self.options)
        cmd.extend(args)
        return cmd

    def _open_subprocess(self, args, passphrase=False):
        # Internal method: open a pipe to a GPG subprocess and return
        # the file objects for communicating with it.
        cmd = self.make_args(args, passphrase)
        if self.verbose:
            pcmd = ' '.join(cmd)
            print(pcmd)
        logger.debug("%s", cmd)
        if not STARTUPINFO:
            si = None
        else:
            si = STARTUPINFO()
            si.dwFlags = STARTF_USESHOWWINDOW
            si.wShowWindow = SW_HIDE
        return Popen(cmd, shell=False, stdin=PIPE, stdout=PIPE, stderr=PIPE,
                     startupinfo=si)

    def _read_response(self, stream, result):
        # Internal method: reads all the stderr output from GPG, taking notice
        # only of lines that begin with the magic [GNUPG:] prefix.
        #
        # Calls methods on the response object for each valid token found,
        # with the arg being the remainder of the status line.
        lines = []
        while True:
            line = stream.readline()
            if len(line) == 0:
                break
            lines.append(line)
            line = line.rstrip()
            if self.verbose:
                print(line)
            logger.debug("%s", line)
            if line[0:9] == '[GNUPG:] ':
                # Chop off the prefix
                line = line[9:]
                L = line.split(None, 1)
                keyword = L[0]
                if len(L) > 1:
                    value = L[1]
                else:
                    value = ""
                result.handle_status(keyword, value)
        result.stderr = ''.join(lines)

    def _read_data(self, stream, result):
        # Read the contents of the file from GPG's stdout
        chunks = []
        while True:
            data = stream.read(1024)
            if len(data) == 0:
                break
            logger.debug("chunk: %r" % data[:256])
            chunks.append(data)
        if _py3k:
            # Join using b'' or '', as appropriate
            result.data = type(data)().join(chunks)
        else:
            result.data = ''.join(chunks)

    def _collect_output(self, process, result, writer=None, stdin=None):
        """
        Drain the subprocesses output streams, writing the collected output
        to the result. If a writer thread (writing to the subprocess) is given,
        make sure it's joined before returning. If a stdin stream is given,
        close it before returning.
        """
        stderr = codecs.getreader(self.encoding)(process.stderr)
        rr = threading.Thread(target=self._read_response, args=(stderr, result))
        rr.setDaemon(True)
        logger.debug('stderr reader: %r', rr)
        rr.start()

        stdout = process.stdout
        dr = threading.Thread(target=self._read_data, args=(stdout, result))
        dr.setDaemon(True)
        logger.debug('stdout reader: %r', dr)
        dr.start()

        dr.join()
        rr.join()
        if writer is not None:
            writer.join()
        process.wait()
        if stdin is not None:
            try:
                stdin.close()
            except IOError:
                pass
        stderr.close()
        stdout.close()

    def _handle_io(self, args, fileobj, result, passphrase=None, binary=False):
        "Handle a call to GPG - pass input data, collect output data"
        # Handle a basic data call - pass data to GPG, handle the output
        # including status information. Garbage In, Garbage Out :)
        p = self._open_subprocess(args, passphrase is not None)
        if not binary:
            stdin = codecs.getwriter(self.encoding)(p.stdin)
        else:
            stdin = p.stdin
        if passphrase:
            _write_passphrase(stdin, passphrase, self.encoding)
        writer = _threaded_copy_data(fileobj, stdin)
        self._collect_output(p, result, writer, stdin)
        return result

    #
    # SIGNATURE METHODS
    #
    def sign(self, message, **kwargs):
        """sign message"""
        f = _make_binary_stream(message, self.encoding)
        result = self.sign_file(f, **kwargs)
        f.close()
        return result

    def set_output_without_confirmation(self, args, output):
        "If writing to a file which exists, avoid a confirmation message."
        if os.path.exists(output):
            # We need to avoid an overwrite confirmation message
            args.extend(['--batch', '--yes'])
        args.extend(['--output', output])

    def sign_file(self, file, keyid=None, passphrase=None, clearsign=True,
                  detach=False, binary=False, output=None):
        """sign file"""
        logger.debug("sign_file: %s", file)
        if binary:
            args = ['-s']
        else:
            args = ['-sa']
        # You can't specify detach-sign and clearsign together: gpg ignores
        # the detach-sign in that case.
        if detach:
            args.append("--detach-sign")
        elif clearsign:
            args.append("--clearsign")
        if keyid:
            args.extend(['--default-key', no_quote(keyid)])
        if output:  # write the output to a file with the specified name
            self.set_output_without_confirmation(args, output)

        result = self.result_map['sign'](self)
        #We could use _handle_io here except for the fact that if the
        #passphrase is bad, gpg bails and you can't write the message.
        p = self._open_subprocess(args, passphrase is not None)
        try:
            stdin = p.stdin
            if passphrase:
                _write_passphrase(stdin, passphrase, self.encoding)
            writer = _threaded_copy_data(file, stdin)
        except IOError:
            logging.exception("error writing message")
            writer = None
        self._collect_output(p, result, writer, stdin)
        return result

    def verify(self, data):
        """Verify the signature on the contents of the string 'data'

        >>> gpg = GPG(gnupghome="keys")
        >>> input = gpg.gen_key_input(Passphrase='foo')
        >>> key = gpg.gen_key(input)
        >>> assert key
        >>> sig = gpg.sign('hello',keyid=key.fingerprint,passphrase='bar')
        >>> assert not sig
        >>> sig = gpg.sign('hello',keyid=key.fingerprint,passphrase='foo')
        >>> assert sig
        >>> verify = gpg.verify(sig.data)
        >>> assert verify

        """
        f = _make_binary_stream(data, self.encoding)
        result = self.verify_file(f)
        f.close()
        return result

    def verify_file(self, file, data_filename=None):
        "Verify the signature on the contents of the file-like object 'file'"
        logger.debug('verify_file: %r, %r', file, data_filename)
        result = self.result_map['verify'](self)
        args = ['--verify']
        if data_filename is None:
            self._handle_io(args, file, result, binary=True)
        else:
            logger.debug('Handling detached verification')
            import tempfile
            fd, fn = tempfile.mkstemp(prefix='pygpg')
            s = file.read()
            file.close()
            logger.debug('Wrote to temp file: %r', s)
            os.write(fd, s)
            os.close(fd)
            args.append(no_quote(fn))
            args.append(no_quote(data_filename))
            try:
                p = self._open_subprocess(args)
                self._collect_output(p, result, stdin=p.stdin)
            finally:
                os.unlink(fn)
        return result

    def verify_data(self, sig_filename, data):
        "Verify the signature in sig_filename against data in memory"
        logger.debug('verify_data: %r, %r ...', sig_filename, data[:16])
        result = self.result_map['verify'](self)
        args = ['--verify', no_quote(sig_filename), '-']
        stream = _make_memory_stream(data)
        self._handle_io(args, stream, result, binary=True)
        return result

    #
    # KEY MANAGEMENT
    #

    def import_keys(self, key_data):
        """ import the key_data into our keyring

        >>> import shutil
        >>> shutil.rmtree("keys")
        >>> gpg = GPG(gnupghome="keys")
        >>> input = gpg.gen_key_input()
        >>> result = gpg.gen_key(input)
        >>> print1 = result.fingerprint
        >>> result = gpg.gen_key(input)
        >>> print2 = result.fingerprint
        >>> pubkey1 = gpg.export_keys(print1)
        >>> seckey1 = gpg.export_keys(print1,secret=True)
        >>> seckeys = gpg.list_keys(secret=True)
        >>> pubkeys = gpg.list_keys()
        >>> assert print1 in seckeys.fingerprints
        >>> assert print1 in pubkeys.fingerprints
        >>> str(gpg.delete_keys(print1))
        'Must delete secret key first'
        >>> str(gpg.delete_keys(print1,secret=True))
        'ok'
        >>> str(gpg.delete_keys(print1))
        'ok'
        >>> str(gpg.delete_keys("nosuchkey"))
        'No such key'
        >>> seckeys = gpg.list_keys(secret=True)
        >>> pubkeys = gpg.list_keys()
        >>> assert not print1 in seckeys.fingerprints
        >>> assert not print1 in pubkeys.fingerprints
        >>> result = gpg.import_keys('foo')
        >>> assert not result
        >>> result = gpg.import_keys(pubkey1)
        >>> pubkeys = gpg.list_keys()
        >>> seckeys = gpg.list_keys(secret=True)
        >>> assert not print1 in seckeys.fingerprints
        >>> assert print1 in pubkeys.fingerprints
        >>> result = gpg.import_keys(seckey1)
        >>> assert result
        >>> seckeys = gpg.list_keys(secret=True)
        >>> pubkeys = gpg.list_keys()
        >>> assert print1 in seckeys.fingerprints
        >>> assert print1 in pubkeys.fingerprints
        >>> assert print2 in pubkeys.fingerprints

        """
        result = self.result_map['import'](self)
        logger.debug('import_keys: %r', key_data[:256])
        data = _make_binary_stream(key_data, self.encoding)
        self._handle_io(['--import'], data, result, binary=True)
        logger.debug('import_keys result: %r', result.__dict__)
        data.close()
        return result

    def recv_keys(self, keyserver, *keyids):
        """Import a key from a keyserver

        >>> import shutil
        >>> shutil.rmtree("keys")
        >>> gpg = GPG(gnupghome="keys")
        >>> os.chmod('keys', 0x1C0)
        >>> result = gpg.recv_keys('keyserver.ubuntu.com', '92905378')
        >>> assert result

        """
        result = self.result_map['import'](self)
        logger.debug('recv_keys: %r', keyids)
        data = _make_binary_stream("", self.encoding)
        #data = ""
        args = ['--keyserver', no_quote(keyserver), '--recv-keys']
        args.extend([no_quote(k) for k in keyids])
        self._handle_io(args, data, result, binary=True)
        logger.debug('recv_keys result: %r', result.__dict__)
        data.close()
        return result

    def send_keys(self, keyserver, *keyids):
        """Send a key to a keyserver.

        Note: it's not practical to test this function without sending
        arbitrary data to live keyservers.
        """
        result = self.result_map['send'](self)
        logger.debug('send_keys: %r', keyids)
        data = _make_binary_stream('', self.encoding)
        #data = ""
        args = ['--keyserver', no_quote(keyserver), '--send-keys']
        args.extend([no_quote(k) for k in keyids])
        self._handle_io(args, data, result, binary=True)
        logger.debug('send_keys result: %r', result.__dict__)
        data.close()
        return result

    def delete_keys(self, fingerprints, secret=False):
        which='key'
        if secret:
            which='secret-key'
        if _is_sequence(fingerprints):
            fingerprints = [no_quote(s) for s in fingerprints]
        else:
            fingerprints = [no_quote(fingerprints)]
        args = ['--batch', '--delete-%s' % which]
        args.extend(fingerprints)
        result = self.result_map['delete'](self)
        p = self._open_subprocess(args)
        self._collect_output(p, result, stdin=p.stdin)
        return result

    def export_keys(self, keyids, secret=False, armor=True, minimal=False):
        "export the indicated keys. 'keyid' is anything gpg accepts"
        which=''
        if secret:
            which='-secret-key'
        if _is_sequence(keyids):
            keyids = [no_quote(k) for k in keyids]
        else:
            keyids = [no_quote(keyids)]
        args = ['--export%s' % which]
        if armor:
            args.append('--armor')
        if minimal:
            args.extend(['--export-options','export-minimal'])
        args.extend(keyids)
        p = self._open_subprocess(args)
        # gpg --export produces no status-fd output; stdout will be
        # empty in case of failure
        #stdout, stderr = p.communicate()
        result = self.result_map['delete'](self) # any result will do
        self._collect_output(p, result, stdin=p.stdin)
        logger.debug('export_keys result: %r', result.data)
        return result.data.decode(self.encoding, self.decode_errors)

    def _get_list_output(self, p, kind):
        # Get the response information
        result = self.result_map[kind](self)
        self._collect_output(p, result, stdin=p.stdin)
        lines = result.data.decode(self.encoding,
                                   self.decode_errors).splitlines()
        valid_keywords = 'pub uid sec fpr sub'.split()
        for line in lines:
            if self.verbose:
                print(line)
            logger.debug("line: %r", line.rstrip())
            if not line:
                break
            L = line.strip().split(':')
            if not L:
                continue
            keyword = L[0]
            if keyword in valid_keywords:
                getattr(result, keyword)(L)
        return result

    def list_keys(self, secret=False):
        """ list the keys currently in the keyring

        >>> import shutil
        >>> shutil.rmtree("keys")
        >>> gpg = GPG(gnupghome="keys")
        >>> input = gpg.gen_key_input()
        >>> result = gpg.gen_key(input)
        >>> print1 = result.fingerprint
        >>> result = gpg.gen_key(input)
        >>> print2 = result.fingerprint
        >>> pubkeys = gpg.list_keys()
        >>> assert print1 in pubkeys.fingerprints
        >>> assert print2 in pubkeys.fingerprints

        """

        which='keys'
        if secret:
            which='secret-keys'
        args = ["--list-%s" % which, "--fixed-list-mode", "--fingerprint",
                "--with-colons"]
        p = self._open_subprocess(args)
        return self._get_list_output(p, 'list')

    def scan_keys(self, filename):
        """
        List details of an ascii armored or binary key file
        without first importing it to the local keyring.

        The function achieves this by running:
        $ gpg --with-fingerprint --with-colons filename
        """
        args = ['--with-fingerprint', '--with-colons']
        args.append(no_quote(filename))
        p = self._open_subprocess(args)
        return self._get_list_output(p, 'scan')

    def search_keys(self, query, keyserver='pgp.mit.edu'):
        """ search keyserver by query (using --search-keys option)

        >>> import shutil
        >>> shutil.rmtree('keys')
        >>> gpg = GPG(gnupghome='keys')
        >>> os.chmod('keys', 0x1C0)
        >>> result = gpg.search_keys('<vinay_sajip@hotmail.com>')
        >>> assert result, 'Failed using default keyserver'
        >>> keyserver = 'keyserver.ubuntu.com'
        >>> result = gpg.search_keys('<vinay_sajip@hotmail.com>', keyserver)
        >>> assert result, 'Failed using keyserver.ubuntu.com'

        """
        query = query.strip()
        if HEX_DIGITS_RE.match(query):
            query = '0x' + query
        args = ['--fixed-list-mode', '--fingerprint', '--with-colons',
                '--keyserver', no_quote(keyserver), '--search-keys',
                no_quote(query)]
        p = self._open_subprocess(args)

        # Get the response information
        result = self.result_map['search'](self)
        self._collect_output(p, result, stdin=p.stdin)
        lines = result.data.decode(self.encoding,
                                   self.decode_errors).splitlines()
        valid_keywords = ['pub', 'uid']
        for line in lines:
            if self.verbose:
                print(line)
            logger.debug('line: %r', line.rstrip())
            if not line:    # sometimes get blank lines on Windows
                continue
            L = line.strip().split(':')
            if not L:
                continue
            keyword = L[0]
            if keyword in valid_keywords:
                getattr(result, keyword)(L)
        return result

    def gen_key(self, input):
        """Generate a key; you might use gen_key_input() to create the
        control input.

        >>> gpg = GPG(gnupghome="keys")
        >>> input = gpg.gen_key_input()
        >>> result = gpg.gen_key(input)
        >>> assert result
        >>> result = gpg.gen_key('foo')
        >>> assert not result

        """
        args = ["--gen-key", "--batch"]
        result = self.result_map['generate'](self)
        f = _make_binary_stream(input, self.encoding)
        self._handle_io(args, f, result, binary=True)
        f.close()
        return result

    def gen_key_input(self, **kwargs):
        """
        Generate --gen-key input per gpg doc/DETAILS
        """
        parms = {}
        for key, val in list(kwargs.items()):
            key = key.replace('_','-').title()
            if str(val).strip():    # skip empty strings
                parms[key] = val
        parms.setdefault('Key-Type','RSA')
        parms.setdefault('Key-Length',2048)
        parms.setdefault('Name-Real', "Autogenerated Key")
        logname = (os.environ.get('LOGNAME') or os.environ.get('USERNAME') or
                   'unspecified')
        hostname = socket.gethostname()
        parms.setdefault('Name-Email', "%s@%s" % (logname.replace(' ', '_'),
                                                  hostname))
        out = "Key-Type: %s\n" % parms.pop('Key-Type')
        for key, val in list(parms.items()):
            out += "%s: %s\n" % (key, val)
        out += "%commit\n"
        return out

        # Key-Type: RSA
        # Key-Length: 1024
        # Name-Real: ISdlink Server on %s
        # Name-Comment: Created by %s
        # Name-Email: isdlink@%s
        # Expire-Date: 0
        # %commit
        #
        #
        # Key-Type: DSA
        # Key-Length: 1024
        # Subkey-Type: ELG-E
        # Subkey-Length: 1024
        # Name-Real: Joe Tester
        # Name-Comment: with stupid passphrase
        # Name-Email: joe@foo.bar
        # Expire-Date: 0
        # Passphrase: abc
        # %pubring foo.pub
        # %secring foo.sec
        # %commit

    #
    # ENCRYPTION
    #
    def encrypt_file(self, file, recipients, sign=None,
            always_trust=False, passphrase=None,
            armor=True, output=None, symmetric=False):
        "Encrypt the message read from the file-like object 'file'"
        args = ['--encrypt']
        if symmetric:
            # can't be False or None - could be True or a cipher algo value
            # such as AES256
            args = ['--symmetric']
            if symmetric is not True:
                args.extend(['--cipher-algo', no_quote(symmetric)])
            # else use the default, currently CAST5
        else:
            if not recipients:
                raise ValueError('No recipients specified with asymmetric '
                                 'encryption')
            if not _is_sequence(recipients):
                recipients = (recipients,)
            for recipient in recipients:
                args.extend(['--recipient', no_quote(recipient)])
        if armor:   # create ascii-armored output - False for binary output
            args.append('--armor')
        if output:  # write the output to a file with the specified name
            self.set_output_without_confirmation(args, output)
        if sign is True:
            args.append('--sign')
        elif sign:
            args.extend(['--sign', '--default-key', no_quote(sign)])
        if always_trust:
            args.append('--always-trust')
        result = self.result_map['crypt'](self)
        self._handle_io(args, file, result, passphrase=passphrase, binary=True)
        logger.debug('encrypt result: %r', result.data)
        return result

    def encrypt(self, data, recipients, **kwargs):
        """Encrypt the message contained in the string 'data'

        >>> import shutil
        >>> if os.path.exists("keys"):
        ...     shutil.rmtree("keys")
        >>> gpg = GPG(gnupghome="keys")
        >>> input = gpg.gen_key_input(passphrase='foo')
        >>> result = gpg.gen_key(input)
        >>> print1 = result.fingerprint
        >>> input = gpg.gen_key_input()
        >>> result = gpg.gen_key(input)
        >>> print2 = result.fingerprint
        >>> result = gpg.encrypt("hello",print2)
        >>> message = str(result)
        >>> assert message != 'hello'
        >>> result = gpg.decrypt(message)
        >>> assert result
        >>> str(result)
        'hello'
        >>> result = gpg.encrypt("hello again",print1)
        >>> message = str(result)
        >>> result = gpg.decrypt(message,passphrase='bar')
        >>> result.status in ('decryption failed', 'bad passphrase')
        True
        >>> assert not result
        >>> result = gpg.decrypt(message,passphrase='foo')
        >>> result.status == 'decryption ok'
        True
        >>> str(result)
        'hello again'
        >>> result = gpg.encrypt("signed hello",print2,sign=print1,passphrase='foo')
        >>> result.status == 'encryption ok'
        True
        >>> message = str(result)
        >>> result = gpg.decrypt(message)
        >>> result.status == 'decryption ok'
        True
        >>> assert result.fingerprint == print1

        """
        data = _make_binary_stream(data, self.encoding)
        result = self.encrypt_file(data, recipients, **kwargs)
        data.close()
        return result

    def decrypt(self, message, **kwargs):
        data = _make_binary_stream(message, self.encoding)
        result = self.decrypt_file(data, **kwargs)
        data.close()
        return result

    def decrypt_file(self, file, always_trust=False, passphrase=None,
                     output=None):
        args = ["--decrypt"]
        if output:  # write the output to a file with the specified name
            self.set_output_without_confirmation(args, output)
        if always_trust:
            args.append("--always-trust")
        result = self.result_map['crypt'](self)
        self._handle_io(args, file, result, passphrase, binary=True)
        logger.debug('decrypt result: %r', result.data)
        return result

