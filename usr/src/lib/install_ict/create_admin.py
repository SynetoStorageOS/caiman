#!/usr/bin/python
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2014, S.C. Syneto S.R.L. All rights reserved.
#

import os
import errno
import shutil
import solaris_install.ict as ICT

class CreateAdmin(ICT.ICTBaseClass):
    ADMIN_HOMEDIR='/var/storage/admin'
    ADMIN_UID_GID='100:1'
    PASSWD_ADMIN_USER="admin:x:100:1::/var/storage/admin:/usr/bin/bash"
    SHADOW_ADMIN_USER='admin:$5$XbQzmjCD$s0goZY3.4lpn.Ln.BG5Q4bVzC.XW95fNcYdPxgs27P1:16400::::::'

    """ ICT checkpoint creates the Syneto StorageOS administrator user """
    def __init__(self, name):
        """Initializes the class
           Parameters:
               -name - this arg is required by the AbstractCheckpoint
                       and is not used by the checkpoint.
        """
        super(CreateAdmin, self).__init__(name)

    def __mkdir_p(self, path):
        try:
            os.makedirs(path)
        except OSError as exception:
            if exception.errno != errno.EEXIST:
                raise

    def __append_line_to_file(self, dry_run, file, line):
        self.logger.debug('Appending "' + line + '" to ' + file)
        if not dry_run:
            with open(file, 'a') as f:
                f.write(line)


    def execute(self, dry_run=False):
        """
            The AbstractCheckpoint class requires this method
            in sub-classes.

            Add 'admin' user with 'admin' password to /etc/{passwd,shadow}
            from the {self.target_dir} directory.
            Also create admin's home directory and files it may need.

            Parameters:
            - dry_run: The default value is False.
                       If set to True, the log message describes the tasks.

            Returns:
            - Nothing
              On failure, errors raised are managed by the engine.
        """

        self.logger.debug('ICT current task: setting up admin user')

        # parse_doc populates variables necessary to execute the checkpoint
        self.parse_doc()

        # should we be more clever and use install_utils.Password class that
        # uses libc to encrypt passwords? this is good enough for now ...
        self.__append_line_to_file(dry_run, self.target_dir + '/etc/passwd', self.PASSWD_ADMIN_USER)
        self.__append_line_to_file(dry_run, self.target_dir + '/etc/shadow', self.SHADOW_ADMIN_USER)

        self.logger.debug('Creating admin home dir: ' + self.target_dir + self.ADMIN_HOMEDIR)
        if not dry_run:
            self.__mkdir_p(self.target_dir + self.ADMIN_HOMEDIR)

        self.logger.debug('Copying admin profile from /etc/skel: ' + self.target_dir + self.ADMIN_HOMEDIR + '/.profile')
        if not dry_run:
            shutil.copy(self.target_dir + '/etc/skel/.profile', self.target_dir + self.ADMIN_HOMEDIR + '/.profile')

        self.logger.debug('Changing ownership to admin user for: ' + self.target_dir + self.ADMIN_HOMEDIR)
        if not dry_run:
            os.chown(self.target_dir + self.ADMIN_HOMEDIR, 100, 1)
            os.chown(self.target_dir + self.ADMIN_HOMEDIR + '/.profile', 100, 1)
