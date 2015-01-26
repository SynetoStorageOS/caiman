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
    MC_INI="""[Midnight-Commander]
editor_fake_half_tabs=1

[Panels]
navigate_with_arrows=true

[terminal:sun-color]
end=^e
home=^a
insert=^b

[terminal:xterm]
end=^e
home=^a
insert=^b
"""
    SUDOERS="""
admin ALL=(ALL) NOPASSWD: ALL
root ALL=(ALL) NOPASSWD: ALL
"""
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

    def __write_string_to_file(self, dry_run, file, str, mode):
        self.logger.debug('Appending "' + str + '" to ' + file)
        if not dry_run:
            with open(file, mode) as f:
                f.write(str + "\n")

    def __recur_chown(self, starting_dir, uid, gid):
        for root, dirs, files in os.walk(starting_dir):
            os.chown(os.path.join(starting_dir, root), uid, gid)
        for f in files:
            os.chown(os.path.join(starting_dir, root, f), uid, gid)


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

        # Update authentication files
        self.__write_string_to_file(dry_run, self.target_dir + '/etc/sudoers', self.SUDOERS, 'a')

        self.logger.debug('Creating admin home directory structure: ' + self.target_dir + self.ADMIN_HOMEDIR + '/.mc')
        if not dry_run:
            self.__mkdir_p(self.target_dir + self.ADMIN_HOMEDIR + '/.mc')

        self.__write_string_to_file(dry_run, self.target_dir + self.ADMIN_HOMEDIR + '/.mc/ini', self.MC_INI, 'w')

        self.logger.debug('Copying admin profile from /etc/skel: ' + self.target_dir + self.ADMIN_HOMEDIR + '/.profile')
        if not dry_run:
            shutil.copy(self.target_dir + '/etc/skel/.profile', self.target_dir + self.ADMIN_HOMEDIR + '/.profile')

        self.logger.debug('Changing ownership to admin user for: ' + self.target_dir + self.ADMIN_HOMEDIR)
        if not dry_run:
            self.__recur_chown( self.target_dir + self.ADMIN_HOMEDIR, 100, 10)
