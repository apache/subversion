#!/usr/bin/env python
#
#  win_repo_bench.py: run repository / server performance tests on Windows.
#
#  Subversion is a tool for revision control.
#  See http://subversion.apache.org for more information.
#
# ====================================================================
#    Licensed to the Apache Software Foundation (ASF) under one
#    or more contributor license agreements.  See the NOTICE file
#    distributed with this work for additional information
#    regarding copyright ownership.  The ASF licenses this file
#    to you under the Apache License, Version 2.0 (the
#    "License"); you may not use this file except in compliance
#    with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing,
#    software distributed under the License is distributed on an
#    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#    KIND, either express or implied.  See the License for the
#    specific language governing permissions and limitations
#    under the License.
######################################################################

# General modules
import os
import shutil
import sys
import subprocess
import time

from win32com.shell import shell, shellcon

# Adapt these paths to your needs

# Contains all the REPOSITORIES
repo_parent = "C:\\repos"

# Where to create working copies
wc_path = "C:\\wc"
exe_path = "C:\\develop\\Subversion\\trunk\\Release"
apache_path = "C:\\develop\\Subversion"

# Test these repositories and in this order.
# Actual repository names have numbers 0 .. REPETITIONS-1 append to them
repositories = ["ruby-f6-nonpacked", "ruby-f7-nonpacked",
                "ruby-f6-packed",    "ruby-f7-packed",
                "bsd-f6-nonpacked",  "bsd-f7-nonpacked",
                "bsd-f6-packed",     "bsd-f7-packed"]

# Basically lists the RA backends to test but as long as all repositories
# can be accessed using any of them, arbitrary URLs are possible.
prefixes = ["svn://localhost/", "http://localhost/svn/", "file:///C:/repos/"]

# Number of time to repeat the tests. For each iteration, there must be
# a separate copy of all repositories.
repetitions = 3

# Server configurations to test
configurations = ['slow', 'medium', 'fast']
svnserve_params = {
  'slow':"",
  'medium':"-M 256" ,
  'fast':"-M 1024 -c 0 --cache-revprops yes --block-read yes --client-speed 1000"
}


def clear_memory():
  """ Clear in-RAM portion of the file / disk cache """
  subprocess.call(["ClearMemory.exe"])

def start_server(prefix, config):
  """ Depending on the url PREFIX, start the corresponding server with the
      given CONFIGuration.  file: and http: access will actually have been
      configured by set_config(). """

  if prefix[:4] == "svn:":
    exe = os.path.join(exe_path, "svnserve.exe")
    command = "cmd.exe /c start " + exe + " -dr " + repo_parent + \
              " " + svnserve_params[config]
    subprocess.call(command)
    time.sleep(2)
  elif prefix[:5] == "http:":
    exe = os.path.join(apache_path, 'bin', 'httpd.exe')
    subprocess.call(exe + " -k start")
    time.sleep(2)

def stop_server(prefix):
  """ Depending on the url PREFIX, stop / kill the corresponding server. """

  if prefix[:4] == "svn:":
    subprocess.call("cmd.exe /c taskkill /im svnserve.exe /f > nul 2>&1")
    time.sleep(1)
  elif prefix[:5] == "http:":
    exe = os.path.join(apache_path, 'bin', 'httpd.exe')
    subprocess.call(exe + " -k stop")
    time.sleep(1)

def run_cs_command(state, config, repository, prefix, args):
  """ Run the client-side command given in ARGS.  Log the STATE of the
      caches, the CONFIG we are using, the REPOSITORY, the url PREFIX
      and finally the execution times. """

  # Make sure we can create a new working copy if we want to.
  if os.path.exists(wc_path):
    shutil.rmtree(wc_path)

  # Select the client to use.
  if ('null-export' in args) or ('null-log' in args):
    exe = os.path.join(exe_path, "svn-bench.exe")
  else:
    exe = os.path.join(exe_path, "svn.exe")

  # Display the operation
  repo_title = repository.replace('nonpacked', 'nopack')
  sys.stdout.write(state, "\t", repo_title, "\t", prefix, "\t", config, "\t ")
  sys.stdout.flush()

  # Execute the command and show the execution times
  subprocess.call(["TimeWin.exe", exe] + args)


def run_test_cs_sequence(config, repository, run, prefix, command, args):
  """ Run the client-side COMMAND with the given ARGS in various stages
      of cache heat-up.  Execute the test with server CONFIG on REPOSITORY
      with the given url PREFIX. """

  # Build the full URL to use.  Exports operate on the main dev line only.
  url = prefix + repository + str(run)
  if (command == 'export') or (command == 'null-export'):
    if repository[:3] == 'bsd':
      url += '/head'
    else:
      url += '/trunk'

  # Full set of command arguments
  args = [command, url] + args

  # Free up caches best we can.
  clear_memory()

  # Caches are quite cool now and ready to take up new data
  start_server(prefix, config)
  run_cs_command("Cold", config, repository, prefix, args)
  stop_server(prefix)

  # OS caches are quite hot now.
  # Run operation from hot OS caches but cold SVN caches.
  start_server(prefix, config)
  run_cs_command("WarmOS", config, repository, prefix, args)
  stop_server(prefix)

  # OS caches may be even hotter now.
  # Run operation from hot OS caches but cold SVN caches.
  start_server(prefix, config)
  run_cs_command("HotOS", config, repository, prefix, args)

  # Keep server process and thus the warmed up SVN caches.
  # Run operation from hot OS and SVN caches.
  run_cs_command("WrmSVN", config, repository, prefix, args)
  run_cs_command("HotSVN", config, repository, prefix, args)
  stop_server(prefix)


def set_config(config):
  """ Switch configuration files to CONFIG.  This overwrites the client
      config file with config.$CONFIG and the server config file with
      subversion.$CONFIG.conf. """

  appdata = shell.SHGetFolderPath(0, shellcon.CSIDL_APPDATA, None, 0)
  svn_config_folder = os.path.join(appdata, 'Subversion')
  svn_config_file = os.path.join(svn_config_folder, 'config')
  svn_config_template = svn_config_file + '.' + config

  shutil.copyfile(svn_config_template, svn_config_file)

  apache_config_folder = os.path.join(apache_path, 'conf', 'extra')
  apache_config_file = os.path.join(apache_config_folder, 'subversion.conf')
  apache_config_template = os.path.join(apache_config_folder,
                                        'subversion.' + config + '.conf')

  shutil.copyfile(apache_config_template, apache_config_file)


def run_test_cs_configurations(command, args):
  """ Run client COMMAND with basic arguments ARGS in all configurations
      repeatedly with all servers on all repositories. """

  print
  print(command)
  print("")

  for config in configurations:
    set_config(config)
    for prefix in prefixes:
      # These two must be the innermost loops and must be in that order.
      # It gives us the coldest caches and the least temporal favoritism.
      for run in range(0, repetitions):
        for repository in repositories:
          run_test_cs_sequence(config, repository, run, prefix, command, args)

def run_admin_command(state, config, repository, args):
  """ Run the svnadmin command given in ARGS.  Log the STATE of the
      caches, the CONFIG we are using, the REPOSITORY and finally
      the execution times. """

  exe = os.path.join(exe_path, "svnadmin.exe")

  if config == 'medium':
    extra = ['-M', '256']
  elif config == 'fast':
    extra = ['-M', '1024']
  else:
    extra = []

  sys.stdout.write(state, "\t", repository, "\t", config, "\t ")
  sys.stdout.flush()
  subprocess.call(["TimeWin.exe", exe] + args + extra)

def run_test_admin_sequence(config, repository, run, command, args):
  """ Run the svnadmin COMMAND with the given ARGS in various stages
      of cache heat-up.  Execute the test with server CONFIG on
      REPOSITORY. """

  # Full set of command arguments
  path = os.path.join(repo_parent, repository + str(run))
  args = [command, path] + args

  # Free up caches best we can.
  clear_memory()

  # svnadmin runs can be quite costly and are usually CPU-bound.
  # Test with "cold" and "hot" CPU caches only.
  run_admin_command("Cold", config, repository, args)
  run_admin_command("Hot", config, repository, args)


def run_test_admin_configurations(command, args):
  """ Run svnadmin COMMAND with basic arguments ARGS in all configurations
      repeatedly on all repositories. """

  print("")
  print(command)
  print("")

  for config in configurations:
    # These two must be the innermost loops and must be in that order.
    # It gives us the coldest caches and the least temporal favoritism.
    for run in range(0, repetitions):
      for repository in repositories:
        run_test_admin_sequence(config, repository, run, command, args)


def bench():
  """ Run all performance tests. """

  run_test_cs_configurations('log', ['-v', '--limit', '50000'])
  run_test_cs_configurations('export', [wc_path, '-q'])

  run_test_cs_configurations('null-log', ['-v', '--limit', '50000', '-q'])
  run_test_cs_configurations('null-export', ['-q'])

  run_test_admin_configurations('dump', ['-q'])

# main function
bench()
