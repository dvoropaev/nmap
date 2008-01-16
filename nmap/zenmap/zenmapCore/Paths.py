#!/usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (C) 2005 Insecure.Com LLC.
#
# Author: Adriano Monteiro Marques <py.adriano@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

from os import R_OK, W_OK, access, mkdir, getcwd, environ, getcwd
from os.path import exists, join, split, abspath, dirname
from tempfile import mktemp
from types import StringTypes

import os.path
import sys

from zenmapCore.UmitLogging import log
from zenmapCore.UmitConfigParser import UmitConfigParser
from zenmapCore.BasePaths import base_paths, HOME
from zenmapCore.TempConf import create_temp_conf_dir
from zenmapCore.I18N import _
from zenmapCore.Version import VERSION
from zenmapCore.Name import APP_NAME

# Look for files relative to the script path to allow running within the build
# directory.
main_dir = os.path.abspath(os.path.dirname(sys.argv[0]))
if hasattr(sys, "frozen"):
    main_dir = dirname(sys.executable)

CONFIG_DIR = join(main_dir, "share", APP_NAME, "config")
LOCALE_DIR = join(main_dir, "share", APP_NAME, "locale")
MISC_DIR = join(main_dir, "share", APP_NAME, "misc")
ICONS_DIR = join(main_dir, "share", "icons")
PIXMAPS_DIR = join(main_dir, "share", "pixmaps")
DOCS_DIR = join(main_dir, "share", APP_NAME, "docs")


#######
# Paths
class Paths(object):
    """Paths
    """
    config_parser = UmitConfigParser()
    paths_section = "paths"

    hardcoded = ["locale_dir",
                 "pixmaps_dir",
                 "icons_dir",
                 "misc_dir",
                 "docs_dir"]

    config_files_list = ["config_file",
                         "scan_profile",
                         "version"]

    empty_config_files_list = ["target_list",
                               "recent_scans",
                               "db"]

    misc_files_list = ["services_dump",
                       "os_dump",
                       "options",
                       "profile_editor",
                       "wizard"]

    other_settings = ["nmap_command_path"]

    config_file_set = False
    
    def set_umit_conf(self, base_dir):
        main_config_dir = ""
        main_config_file = ""
        if exists(CONFIG_DIR) and \
            exists(join(CONFIG_DIR, base_paths['config_file'])):
            main_config_dir = CONFIG_DIR

        elif exists(join(base_dir, CONFIG_DIR)) and\
            exists(join(base_dir,
                        CONFIG_DIR,
                        base_paths['config_file'])):
            main_config_dir = join(base_dir, CONFIG_DIR)

        elif exists(join(split(base_dir)[0], CONFIG_DIR)) and \
            exists(join(split(base_dir)[0],
                        CONFIG_DIR,
                        base_paths['config_file'])):
            main_config_dir = join(split(base_dir)[0], CONFIG_DIR)

        else:
            main_config_dir = create_temp_conf_dir(VERSION)

        # Main config file, based on the main_config_dir got above
        main_config_file = join(main_config_dir, base_paths['config_file'])

        # This is the expected place in which the configuration file should be
        # placed
        supposed_file = join(base_paths['user_dir'], base_paths['config_file'])
        config_dir = ""
        config_file = ""

        if exists(supposed_file)\
               and check_access(supposed_file, R_OK and W_OK):
            config_dir = base_paths['user_dir']
            config_file = supposed_file
            log.debug(">>> Using config files in user home \
directory: %s" % config_file)

        elif not exists(supposed_file)\
             and not check_access(base_paths['user_dir'],
                                  R_OK and W_OK):
            try:
                result = create_user_dir(join(main_config_dir,
                                              base_paths['config_file']),
                                         HOME)
                if type(result) == type({}):
                    config_dir = result['config_dir']
                    config_file = result['config_file']
                    log.debug(">>> Using recently created config files in \
user home: %s" % config_file)
                else:
                    raise Exception()
            except:
                log.debug(">>> Failed to create user home")

        if config_dir and config_file:
            # Checking if the version of the configuration files are the same
            # as this program's version
            if not self.check_version(config_dir):
                log.debug(">>> The config files versions are different!")
                log.debug(">>> Running update scripts...")
                self.update_config_dir(config_dir)

        else:
            log.debug(">>> There is no way to create nor use home connfigs.")
            log.debug(">>> Trying to use main configuration files...")

            config_dir = main_config_dir
            config_file = main_config_file

        # Parsing the main config file
        self.config_parser.read(config_file)

        # Should make the following only after reading the conf file
        self.config_dir = config_dir
        self.config_file = config_file
        self.config_file_set = True
        self.locale_dir = LOCALE_DIR
        self.pixmaps_dir = PIXMAPS_DIR
        self.icons_dir = ICONS_DIR
        self.misc_dir = MISC_DIR
        self.docs_dir = DOCS_DIR

        log.debug(">>> Config file: %s" % config_file)

    def update_config_dir(self, config_dir):
        # Do any updates of configuration files. Not yet implemented.
        pass

    def check_version(self, config_dir):
        version_file = join(config_dir, base_paths['version'])

        if exists(version_file):
            ver = open(version_file).readline().strip()

            log.debug(">>> This Version: %s" % VERSION)
            log.debug(">>> Version of the files in %s: %s" % (config_dir, ver))

            if VERSION == ver:
                return True
        return False

    def root_dir(self):
        """Retrieves root dir on current filesystem"""
        curr_dir = getcwd()
        while True:
            splited = split(curr_dir)[0]
            if curr_dir == splited:
                break
            curr_dir = splited

        log.debug(">>> Root dir: %s" % curr_dir)
        return curr_dir

    def __getattr__(self, name):
        if self.config_file_set:
            if name in self.other_settings:
                return self.config_parser.get(self.paths_section, name)

            elif name in self.hardcoded:
                return self.__dict__[name]

            elif name in self.config_files_list:
                return return_if_exists(join(self.__dict__['config_dir'],
                                             base_paths[name]))

            elif name in self.empty_config_files_list:
                return return_if_exists(join(self.__dict__['config_dir'],
                                             base_paths[name]),
                                        True)

            elif name in self.misc_files_list:
                return return_if_exists(join(self.__dict__["misc_dir"],
                                                     base_paths[name]))

            try:
                return self.__dict__[name]
            except:
                raise NameError(name)
        else:
            raise Exception("Must set config file location first")

    def __setattr__(self, name, value):
        if name in self.other_settings:
            self.config_parser.set(self.paths_section, name, value)
        else:
            self.__dict__[name] = value

####################################
# Functions for directories creation

def create_user_dir(config_file, user_home):
    log.debug(">>> Create user dir at given home: %s" % user_home)
    log.debug(">>> Using %s as source" % config_file)

    main_umit_conf = UmitConfigParser()
    main_umit_conf.read(config_file)
    paths_section = "paths"

    user_dir = join(user_home, base_paths['config_dir'])

    if exists(user_home)\
           and access(user_home, R_OK and W_OK)\
           and not exists(user_dir):
        mkdir(user_dir)
        log.debug(">>> User dir successfully created! %s" % user_dir)
    else:
        log.warning(">>> No permissions to create user dir!")
        return False

    main_dir = split(config_file)[0]
    copy_config_file("scan_profile.usp", main_dir, user_dir)
    copy_config_file(APP_NAME + "_version", main_dir, user_dir)

    return dict(user_dir = user_dir,
                config_dir = user_dir,
                config_file = copy_config_file(APP_NAME + ".conf",
                                               split(config_file)[0],
                                               user_dir))

def copy_config_file(filename, dir_origin, dir_destiny):
    log.debug(">>> copy_config_file %s to %s" % (filename, dir_destiny))

    origin = join(dir_origin, filename)
    destiny = join(dir_destiny, filename)

    if not exists(destiny):
        # Quick copy
        origin_file = open(origin, 'r')
        destiny_file = open(destiny, 'w')
        destiny_file.write(origin_file.read())
        origin_file.close()
        destiny_file.close()
    return destiny

def check_access(path, permission):
    if type(path) in StringTypes:
        return exists(path) and access(path, permission)
    return False

def return_if_exists(path, create=False):
    path = abspath(path)
    if exists(path):
        return path
    elif create:
        f = open(path, "w")
        f.close()
        return path
    raise Exception("File '%s' does not exist or could not be found!" % path)

############
# Singleton!
Path = Paths()

if __name__ == '__main__':
    Path.set_umit_conf(split(sys.argv[0])[0])

    print ">>> SAVED DIRECTORIES:"
    print ">>> LOCALE DIR:", Path.locale_dir
    print ">>> PIXMAPS DIR:", Path.pixmaps_dir
    print ">>> CONFIG DIR:", Path.config_dir
    print
    print ">>> FILES:"
    print ">>> CONFIG FILE:", Path.config_file
    print ">>> TARGET_LIST:", Path.target_list
    print ">>> PROFILE_EDITOR:", Path.profile_editor
    print ">>> WIZARD:", Path.wizard
    print ">>> SCAN_PROFILE:", Path.scan_profile
    print ">>> RECENT_SCANS:", Path.recent_scans
    print ">>> OPTIONS:", Path.options
    print
    print ">>> DB:", Path.db
    print ">>> SERVICES DUMP:", Path.services_dump
    print ">>> OS DB DUMP:", Path.os_dump
    print ">>> VERSION:", Path.version
    print ">>> NMAP COMMAND PATH:", Path.nmap_command_path
