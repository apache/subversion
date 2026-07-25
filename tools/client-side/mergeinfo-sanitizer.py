#!/usr/bin/env python
#
# ====================================================================
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
# ====================================================================
import svn
import sys
import os
import getopt
import hashlib
import pickle
import getpass
from svn import client, core, ra, wc

## This script first fetches the mergeinfo of the working copy and tries
## to fetch the location segments for the source paths in the respective
## revisions present in the mergeinfo. With the obtained location segments
## result, it creates a new mergeinfo. The depth is infinity by default.
## This script would stop proceeding if there are any local modifications in the
## working copy.

try:
  my_getopt = getopt.gnu_getopt
except AttributeError:
  my_getopt = getopt.getopt
mergeinfo = {}

def usage():
  sys.stderr.write(""" Usage: %s WCPATH [OPTION]

Analyze the mergeinfo property of the given WCPATH.
Look for the existence of merge_source's locations at their recorded
merge ranges. If non-existent merge source is found fix the mergeinfo.

Valid Options:
 -f   [--fix]      : set the svn:mergeinfo property. Not committing the changes.
 -h   [--help]     : display the usage

""" % os.path.basename(sys.argv[0]) )


##
# This function would 'svn propset' the new mergeinfo to the working copy
##
def set_new_mergeinfo(wcpath, newmergeinfo, ctx):
  client.propset3("svn:mergeinfo", newmergeinfo, wcpath, core.svn_depth_empty,
                   0, core.SVN_INVALID_REVNUM, None, None, ctx)


##
# Returns the md5 hash of the file
##
def md5_of_file(f, block_size = 2*20):
  md5 = hashlib.md5()
  while True:
    data = f.read(block_size)
    if not data:
      break
    md5.update(data)
  return md5.digest()



def hasher(hash_file, newmergeinfo_file):
  new_mergeinfo =  core.svn_mergeinfo_to_string(mergeinfo)
  with open(newmergeinfo_file, "a") as buffer_file:
    pickle.dump(new_mergeinfo, buffer_file)
  buffer_file.close()

  with open(newmergeinfo_file, "rb") as buffer_file:
    hash_of_buffer_file = md5_of_file(buffer_file)
  buffer_file.close()

  with open(hash_file, "w") as hash_file:
    pickle.dump(hash_of_buffer_file, hash_file)
  hash_file.close()


def location_segment_callback(segment, pool):
  if segment.path is not None:
    source_path = '/' + segment.path
    path_ranges = mergeinfo.get(source_path, [])
    range = svn.core.svn_merge_range_t()
    range.start = segment.range_start - 1
    range.end = segment.range_end
    range.inheritable = 1
    path_ranges.append(range)
    mergeinfo[source_path] = path_ranges

##
# This function does the authentication in an interactive way
##
def prompt_func_ssl_unknown_cert(realm, failures, cert_info, may_save, pool):
  print("The certificate details are as follows:")
  print("--------------------------------------")
  print("Issuer     : " + str(cert_info.issuer_dname))
  print("Hostname   : " + str(cert_info.hostname))
  print("ValidFrom  : " + str(cert_info.valid_from))
  print("ValidUpto  : " + str(cert_info.valid_until))
  print("Fingerprint: " + str(cert_info.fingerprint))
  print("")
  ssl_trust = core.svn_auth_cred_ssl_server_trust_t()
  if may_save:
    choice = raw_input( "accept (t)temporarily   (p)permanently: ")
  else:
    choice = raw_input( "(r)Reject or accept (t)temporarily: ")
  if choice[0] == "t" or choice[0] == "T":
    ssl_trust.may_save = False
    ssl_trust.accepted_failures = failures
  elif choice[0] == "p" or choice[0] == "P":
    ssl_trust.may_save = True
    ssl_trust.accepted_failures = failures
  else:
    ssl_trust = None
  return ssl_trust

def prompt_func_simple_prompt(realm, username, may_save, pool):
  username = raw_input("username: ")
  password = getpass.getpass(prompt="password: ")
  simple_cred = core.svn_auth_cred_simple_t()
  simple_cred.username = username
  simple_cred.password = password
  simple_cred.may_save = False
  return simple_cred

##
# This function tries to authenticate(if needed) and fetch the
# location segments for the available mergeinfo and create a new
# mergeinfo dictionary
##
def get_new_location_segments(parsed_original_mergeinfo, repo_root,
                              wcpath, ctx):

    for path in parsed_original_mergeinfo:
      full_url = repo_root + path
      ra_callbacks = ra.callbacks_t()
      ra_callbacks.auth_baton = core.svn_auth_open([
                                   core.svn_auth_get_ssl_server_trust_file_provider(),
                                   core.svn_auth_get_simple_prompt_provider(prompt_func_simple_prompt, 2),
                                   core.svn_auth_get_ssl_server_trust_prompt_provider(prompt_func_ssl_unknown_cert),
                                   svn.client.get_simple_provider(),
                                   svn.client.get_username_provider()
                                    ])
      try:
        ctx.config = core.svn_config_get_config(None)
        ra_session = ra.open(full_url, ra_callbacks, None, ctx.config)

        for revision_range in parsed_original_mergeinfo[path]:
          try:
            ra.get_location_segments(ra_session, "", revision_range.end,
                                     revision_range.end, revision_range.start + 1, location_segment_callback)
          except svn.core.SubversionException:
            sys.stderr.write(" Could not find location segments for %s \n" % path)
      except Exception as e:
        sys.stderr.write("")


def sanitize_mergeinfo(parsed_original_mergeinfo, repo_root, wcpath,
                       ctx, hash_file, newmergeinfo_file, temp_pool):
  full_mergeinfo = {}
  for entry in parsed_original_mergeinfo:
    get_new_location_segments(parsed_original_mergeinfo[entry], repo_root, wcpath, ctx)
    full_mergeinfo.update(parsed_original_mergeinfo[entry])

  hasher(hash_file, newmergeinfo_file)
  diff_mergeinfo = core.svn_mergeinfo_diff(full_mergeinfo,
                                           mergeinfo, 1, temp_pool)
  #There should be no mergeinfo added by our population. There should only
  #be deletion of mergeinfo. so take it from diff_mergeinfo[0]
  print("The bogus mergeinfo summary:")
  bogus_mergeinfo_deleted = diff_mergeinfo[0]
  for bogus_mergeinfo_path in bogus_mergeinfo_deleted:
    sys.stdout.write(bogus_mergeinfo_path + ": ")
    for revision_range in bogus_mergeinfo_deleted[bogus_mergeinfo_path]:
      sys.stdout.write(str(revision_range.start + 1) + "-" + str(revision_range.end) + ",")
    print("")

##
# This function tries to 'propset the new mergeinfo into the working copy.
# It reads the new mergeinfo from the .newmergeinfo file and verifies its
# hash against the hash in the .hashfile
##
def fix_sanitized_mergeinfo(parsed_original_mergeinfo, repo_root, wcpath,
                            ctx, hash_file, newmergeinfo_file, temp_pool):
  has_local_modification = check_local_modifications(wcpath, temp_pool)
  old_hash = ''
  new_hash = ''
  try:
    with open(hash_file, "r") as f:
      old_hash = pickle.load(f)
    f.close
  except IOError as e:
    get_new_location_segments(parsed_original_mergeinfo, repo_root, wcpath, ctx)
    hasher(hash_file, newmergeinfo_file)
    try:
      with open(hash_file, "r") as f:
        old_hash = pickle.load(f)
      f.close
    except IOError:
      hasher(hash_file, newmergeinfo_file)
  try:
    with open(newmergeinfo_file, "r") as f:
      new_hash = md5_of_file(f)
    f.close
  except IOError as e:
    if not mergeinfo:
      get_new_location_segments(parsed_original_mergeinfo, repo_root, wcpath, ctx)
    hasher(hash_file, newmergeinfo_file)
    with open(newmergeinfo_file, "r") as f:
      new_hash = md5_of_file(f)
    f.close
  if old_hash == new_hash:
    with open(newmergeinfo_file, "r") as f:
      newmergeinfo = pickle.load(f)
    f.close
    set_new_mergeinfo(wcpath, newmergeinfo, ctx)
    if os.path.exists(newmergeinfo_file):
      os.remove(newmergeinfo_file)
      os.remove(hash_file)
  else:
    print("The hashes are not matching. Probable chance of unwanted tweaking in the mergeinfo")


##
# This function checks the working copy for any local modifications
##
def check_local_modifications(wcpath, temp_pool):
  has_local_mod = wc.svn_wc_revision_status(wcpath, None, 0, None, temp_pool)
  if has_local_mod.modified:
    print("""The working copy has local modifications. Please revert them or clean
the working copy before running the script.""")
    sys.exit(1)

def get_original_mergeinfo(wcpath, revision, depth, ctx, temp_pool):
  propget_list = client.svn_client_propget3("svn:mergeinfo", wcpath,
                                            revision, revision, depth, None,
                                            ctx, temp_pool)

  pathwise_mergeinfo = ""
  pathwise_mergeinfo_list = []
  mergeinfo_catalog = propget_list[0]
  mergeinfo_catalog_dict = {}
  for entry in mergeinfo_catalog:
      mergeinfo_catalog_dict[entry] = core.svn_mergeinfo_parse(mergeinfo_catalog[entry], temp_pool)
  return mergeinfo_catalog_dict


def main():
  try:
    opts, args = my_getopt(sys.argv[1:], "h?f", ["help", "fix"])
  except Exception as e:
    sys.stderr.write(""" Improperly used """)
    sys.exit(1)

  if len(args) == 1:
   wcpath = args[0]
   wcpath = os.path.abspath(wcpath)
  else:
    usage()
    sys.exit(1)

  fix = 0
  current_path = os.getcwd()
  hash_file = os.path.join(current_path, ".hashfile")
  newmergeinfo_file = os.path.join(current_path, ".newmergeinfo")

  temp_pool = core.svn_pool_create()
  ctx = client.svn_client_create_context(temp_pool)
  depth = core.svn_depth_infinity
  revision = core.svn_opt_revision_t()
  revision.kind = core.svn_opt_revision_unspecified

  for opt, values in opts:
    if opt == "--help" or opt in ("-h", "-?"):
      usage()
    elif opt == "--fix" or opt == "-f":
      fix = 1

  # Check for any local modifications in the working copy
  check_local_modifications(wcpath, temp_pool)

  parsed_original_mergeinfo = get_original_mergeinfo(wcpath, revision,
                                                     depth, ctx, temp_pool)

  repo_root = client.svn_client_root_url_from_path(wcpath, ctx, temp_pool)

  core.svn_config_ensure(None)

  if fix == 0:
    sanitize_mergeinfo(parsed_original_mergeinfo, repo_root, wcpath, ctx,
                       hash_file, newmergeinfo_file, temp_pool)
  if fix == 1:
    fix_sanitized_mergeinfo(parsed_original_mergeinfo, repo_root, wcpath,
                            ctx, hash_file, newmergeinfo_file, temp_pool)


if __name__ == "__main__":
  try:
    main()
  except KeyboardInterrupt:
    print("")
    sys.stderr.write("The script is interrupted and stopped manually.")
    print("")

