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
# Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
#
import sys
import osol_install.distro_const.DC_checkpoint as DC_checkpoint
import shutil

length = len(sys.argv)
if length < 7:
        raise Exception, ("finalizer_checkpoint: At least 6 args "
	    "are required: Reader socket, build area mntpt, manifest file, "
	    "state file, zfs dataset(s), message")


manifest_file = sys.argv[3]
state_file = sys.argv[4]
zfs_snapshots = sys.argv[5:length-1]
message = sys.argv[length-1]

for snapshot in zfs_snapshots:
	DC_checkpoint.shell_cmd("/usr/sbin/zfs snapshot " + snapshot)

shutil.copy(manifest_file, state_file)
print message
sys.exit(0)
