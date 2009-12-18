#!/usr/bin/env python
#
# Copyright 2005 Branko Cibej <brane@xbc.nu>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

'''mod_python authorization handler for mod_authz_svn groups.

   This handler reads group definitions from a mod_authz_svn access
   configuration file and does an authz check against that information.

   Supported Require directives:

     - Require valid-user
       Checks if the authenticated user is mentioned in any of the groups.
       Note that this is authorization, not authentication; so, a user may
       have been authenticated correctly, yet still fail this test if
       she is not mentioned in the authz config file.

     - Require group name...
       Check if the authenticated user is a member of any of the named
       groups.

     - Require user name...
       Ignored. The authentication handlers are supposed to check this.

   Configuration:

     <Location ...>
       PythonAuthzHandler authz_svn_group
       PythonOption AuthzSVNGroupFile /path/to/file
       PythonOption AuthzSVNGroupAuthoritative Yes/On/1|No/Off/0
       ...
     </Location>

     AuthzSVNGroupFile: Path to the mod_authz_svn configuration file.
     AuthzSVNGroupAuthoritative: If turned off, authz_svn_group.py will
       return DECLINED rather than HTTP_FORBIDDEN if a Require
       directive is not satisfied.
'''

import os, sys
import ConfigParser
from mod_python import apache

class __authz_info:
  '''Encapsulation of group info from the mod_authz_svn access file.'''

  def __init__(self, authz_file):
    '''Parse the SVN access file.'''
    self.__groups = {}
    self.__users = {}
    cfg = ConfigParser.ConfigParser()
    cfg.read(authz_file)
    if cfg.has_section('groups'):
      self.__init_groups(cfg)

  def __init_groups(self, cfg):
    '''Compute user and group membership.'''
    group_list = cfg.options('groups')
    group_map = {}
    for group in group_list:
      names = map(lambda x: x.strip(),
                  cfg.get('groups', group).split(','))
      group_map[group] = names
      for name in names:
        if not name.startswith('@'):
          self.__users[name] = None
    for group in group_list:
      self.__groups[group] = self.__expand_group_users(group, group_map)

  def __expand_group_users(self, group, group_map):
    '''Return the complete (recursive) list of users that belong to
    a particular group, as a map.'''
    users = {}
    for name in group_map[group]:
      if not name.startswith('@'):
        users[name] = None
      else:
        users.update(self.__expand_group_users(name[1:], group_map))
    return users

  def is_valid_user(self, user):
    '''Return True if the user is valid.'''
    return self.__users.has_key(user)

  def is_user_in_group(self, user, group):
    '''Return True if the user is in a particular group.'''
    return (self.__groups.has_key(group)
            and self.__groups[group].has_key(user))


class __config:
  '''Handler configuration'''

  AUTHZ_FILE = 'AuthzSVNGroupFile'
  AUTHORITATIVE = 'AuthzSVNGroupAuthoritative'

  def __init__(self, req):
    self.__authz_file = None
    self.__authoritative = True
    cfg = req.get_options()

    if cfg.has_key(self.AUTHZ_FILE):
      self.__authz_file = cfg[self.AUTHZ_FILE]
      if not os.path.exists(self.__authz_file):
        req.log_error(('%s: "%s" not found'
                       % (self.AUTHZ_FILE, self.__authz_file)),
                      apache.APLOG_ERR)
        raise apache.SERVER_RETURN, apache.HTTP_INTERNAL_SERVER_ERROR

    if cfg.has_key(self.AUTHORITATIVE):
      authcfg = cfg[self.AUTHORITATIVE].lower()
      if authcfg in ['yes', 'on', '1']:
        self.__authoritative = True
      elif authcfg in ['no', 'off', '0']:
        self.__authoritative = False
      else:
        req.log_error(('%s: invalid value "%s"'
                       % (self.AUTHORITATIVE, cfg[self.AUTHORITATIVE])),
                      apache.APLOG_ERR)
        raise apache.SERVER_RETURN, apache.HTTP_INTERNAL_SERVER_ERROR
    pass

  def authz_file(self):
    return self.__authz_file

  def authoritative(self):
    return self.__authoritative


def __init_authz_info(req, cfg):
  '''Initialize the global authz info if it is not available yet.
  Return False if this module is disabled.'''
  if not globals().has_key('__authz_svn_group_info'):
    if cfg.authz_file() is None:
      return False
    global __authz_svn_group_info
    __authz_svn_group_info = __authz_info(cfg.authz_file())
  return True


def authzhandler(req):
  '''The authorization handler.'''
  cfg = __config(req)
  if not __init_authz_info(req, cfg):
    return apache.DECLINED

  if cfg.authoritative():
    forbidden = apache.HTTP_FORBIDDEN
  else:
    forbidden = apache.DECLINED

  req.get_basic_auth_pw()
  for requires in req.requires():
    if requires == 'valid-user':
      if not __authz_svn_group_info.is_valid_user(req.user):
        return forbidden
    elif requires.startswith('group '):
      for group in requires.split()[1:]:
        if __authz_svn_group_info.is_user_in_group(req.user, group):
          break
      else:
        return forbidden
    elif requires.startswith('user '):
      pass                             # Handled by the authen handler
    else:
      req.log_error('Unknown directive "Require %s"' % requires,
                    apache.APLOG_ERR)
      return apache.HTTP_INTERNAL_SERVER_ERROR

  return apache.OK
