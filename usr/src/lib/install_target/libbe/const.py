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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

""" constants and enums from libbe.h
"""

BE_SUCCESS = 0
BE_ERRNO = ( \
    BE_ERR_ACCESS,
    BE_ERR_ACTIVATE_CURR,
    BE_ERR_AUTONAME,
    BE_ERR_BE_NOENT,
    BE_ERR_BUSY,
    BE_ERR_CANCELED,
    BE_ERR_CLONE,
    BE_ERR_COPY,
    BE_ERR_CREATDS,
    BE_ERR_CURR_BE_NOT_FOUND,
    BE_ERR_DESTROY,
    BE_ERR_DEMOTE,
    BE_ERR_DSTYPE,
    BE_ERR_BE_EXISTS,
    BE_ERR_INIT,
    BE_ERR_INTR,
    BE_ERR_INVAL,
    BE_ERR_INVALPROP,
    BE_ERR_INVALMOUNTPOINT,
    BE_ERR_MOUNT,
    BE_ERR_MOUNTED,
    BE_ERR_NAMETOOLONG,
    BE_ERR_NOENT,
    BE_ERR_POOL_NOENT,
    BE_ERR_NODEV,
    BE_ERR_NOTMOUNTED,
    BE_ERR_NOMEM,
    BE_ERR_NONINHERIT,
    BE_ERR_NXIO,
    BE_ERR_NOSPC,
    BE_ERR_NOTSUP,
    BE_ERR_OPEN,
    BE_ERR_PERM,
    BE_ERR_UNAVAIL,
    BE_ERR_PROMOTE,
    BE_ERR_ROFS,
    BE_ERR_READONLYDS,
    BE_ERR_READONLYPROP,
    BE_ERR_SS_EXISTS,
    BE_ERR_SS_NOENT,
    BE_ERR_UMOUNT,
    BE_ERR_UMOUNT_CURR_BE,
    BE_ERR_UMOUNT_SHARED,
    BE_ERR_UNKNOWN,
    BE_ERR_ZFS,
    BE_ERR_DESTROY_CURR_BE,
    BE_ERR_GEN_UUID,
    BE_ERR_PARSE_UUID,
    BE_ERR_NO_UUID,
    BE_ERR_ZONE_NO_PARENTBE,
    BE_ERR_ZONE_MULTIPLE_ACTIVE,
    BE_ERR_ZONE_NO_ACTIVE_ROOT,
    BE_ERR_ZONE_ROOT_NOT_LEGACY,
    BE_ERR_NO_MOUNTED_ZONE,
    BE_ERR_MOUNT_ZONEROOT,
    BE_ERR_UMOUNT_ZONEROOT,
    BE_ERR_ZONES_UNMOUNT,
    BE_ERR_FAULT,
    BE_ERR_RENAME_ACTIVE,
    BE_ERR_NO_MENU,
    BE_ERR_DEV_BUSY,
    BE_ERR_BAD_MENU_PATH,
    BE_ERR_ZONE_SS_EXISTS,
    BE_ERR_PKG_VERSION,
    BE_ERR_ADD_SPLASH_ICT,
    BE_ERR_PKG,
    BE_ERR_BOOTFILE_INST,
    BE_ERR_EXTCMD,
    BE_ERR_ZONE_ROOT_NOT_SLASH,
    BE_ERR_ZONE_NOTSUP,
    BE_ERR_ZONE_MPOOL_NOTSUP,
    BE_ERR_ZONE_RO_NOTSUP,
    BE_ERR_NO_MENU_ENTRY,
    BE_ERR_NESTED_PYTHON,
    BE_ERR_PYTHON,
    BE_ERR_PYTHON_EXCEPT,
    BE_ERR_RENAME_ACTIVE_ON_BOOT,
    BE_ERR_NO_RPOOLS
) = xrange(4000, 4078)

BE_ERRNO_MAP = {
    BE_SUCCESS: "BE_SUCCESS",
    BE_ERR_ACCESS: "BE_ERR_ACCESS",
    BE_ERR_ACTIVATE_CURR: "BE_ERR_ACTIVATE_CURR",
    BE_ERR_AUTONAME: "BE_ERR_AUTONAME",
    BE_ERR_BE_NOENT: "BE_ERR_BE_NOENT",
    BE_ERR_BUSY: "BE_ERR_BUSY",
    BE_ERR_CANCELED: "BE_ERR_CANCELED",
    BE_ERR_CLONE: "BE_ERR_CLONE",
    BE_ERR_COPY: "BE_ERR_COPY",
    BE_ERR_CREATDS: "BE_ERR_CREATDS",
    BE_ERR_CURR_BE_NOT_FOUND: "BE_ERR_CURR_BE_NOT_FOUND",
    BE_ERR_DESTROY: "BE_ERR_DESTROY",
    BE_ERR_DEMOTE: "BE_ERR_DEMOTE",
    BE_ERR_DSTYPE: "BE_ERR_DSTYPE",
    BE_ERR_BE_EXISTS: "BE_ERR_BE_EXISTS",
    BE_ERR_INIT: "BE_ERR_INIT",
    BE_ERR_INTR: "BE_ERR_INTR",
    BE_ERR_INVAL: "BE_ERR_INVAL",
    BE_ERR_INVALPROP: "BE_ERR_INVALPROP",
    BE_ERR_INVALMOUNTPOINT: "BE_ERR_INVALMOUNTPOINT",
    BE_ERR_MOUNT: "BE_ERR_MOUNT",
    BE_ERR_MOUNTED: "BE_ERR_MOUNTED",
    BE_ERR_NAMETOOLONG: "BE_ERR_NAMETOOLONG",
    BE_ERR_NOENT: "BE_ERR_NOENT",
    BE_ERR_POOL_NOENT: "BE_ERR_POOL_NOENT",
    BE_ERR_NODEV: "BE_ERR_NODEV",
    BE_ERR_NOTMOUNTED: "BE_ERR_NOTMOUNTED",
    BE_ERR_NOMEM: "BE_ERR_NOMEM",
    BE_ERR_NONINHERIT: "BE_ERR_NONINHERIT",
    BE_ERR_NXIO: "BE_ERR_NXIO",
    BE_ERR_NOSPC: "BE_ERR_NOSPC",
    BE_ERR_NOTSUP: "BE_ERR_NOTSUP",
    BE_ERR_OPEN: "BE_ERR_OPEN",
    BE_ERR_PERM: "BE_ERR_PERM",
    BE_ERR_UNAVAIL: "BE_ERR_UNAVAIL",
    BE_ERR_PROMOTE: "BE_ERR_PROMOTE",
    BE_ERR_ROFS: "BE_ERR_ROFS",
    BE_ERR_READONLYDS: "BE_ERR_READONLYDS",
    BE_ERR_READONLYPROP: "BE_ERR_READONLYPROP",
    BE_ERR_SS_EXISTS: "BE_ERR_SS_EXISTS",
    BE_ERR_SS_NOENT: "BE_ERR_SS_NOENT",
    BE_ERR_UMOUNT: "BE_ERR_UMOUNT",
    BE_ERR_UMOUNT_CURR_BE: "BE_ERR_UMOUNT_CURR_BE",
    BE_ERR_UMOUNT_SHARED: "BE_ERR_UMOUNT_SHARED",
    BE_ERR_UNKNOWN: "BE_ERR_UNKNOWN",
    BE_ERR_ZFS: "BE_ERR_ZFS",
    BE_ERR_DESTROY_CURR_BE: "BE_ERR_DESTROY_CURR_BE",
    BE_ERR_GEN_UUID: "BE_ERR_GEN_UUID",
    BE_ERR_PARSE_UUID: "BE_ERR_PARSE_UUID",
    BE_ERR_NO_UUID: "BE_ERR_NO_UUID",
    BE_ERR_ZONE_NO_PARENTBE: "BE_ERR_ZONE_NO_PARENTBE",
    BE_ERR_ZONE_MULTIPLE_ACTIVE: "BE_ERR_ZONE_MULTIPLE_ACTIVE",
    BE_ERR_ZONE_NO_ACTIVE_ROOT: "BE_ERR_ZONE_NO_ACTIVE_ROOT",
    BE_ERR_ZONE_ROOT_NOT_LEGACY: "BE_ERR_ZONE_ROOT_NOT_LEGACY",
    BE_ERR_NO_MOUNTED_ZONE: "BE_ERR_NO_MOUNTED_ZONE",
    BE_ERR_MOUNT_ZONEROOT: "BE_ERR_MOUNT_ZONEROOT",
    BE_ERR_UMOUNT_ZONEROOT: "BE_ERR_UMOUNT_ZONEROOT",
    BE_ERR_ZONES_UNMOUNT: "BE_ERR_ZONES_UNMOUNT",
    BE_ERR_FAULT: "BE_ERR_FAULT",
    BE_ERR_RENAME_ACTIVE: "BE_ERR_RENAME_ACTIVE",
    BE_ERR_NO_MENU: "BE_ERR_NO_MENU",
    BE_ERR_DEV_BUSY: "BE_ERR_DEV_BUSY",
    BE_ERR_BAD_MENU_PATH: "BE_ERR_BAD_MENU_PATH",
    BE_ERR_ZONE_SS_EXISTS: "BE_ERR_ZONE_SS_EXISTS",
    BE_ERR_PKG_VERSION: "BE_ERR_PKG_VERSION",
    BE_ERR_ADD_SPLASH_ICT: "BE_ERR_ADD_SPLASH_ICT",
    BE_ERR_PKG: "BE_ERR_PKG",
    BE_ERR_BOOTFILE_INST: "BE_ERR_BOOTFILE_INST",
    BE_ERR_EXTCMD: "BE_ERR_EXTCMD",
    BE_ERR_ZONE_ROOT_NOT_SLASH: "BE_ERR_ZONE_ROOT_NOT_SLASH",
    BE_ERR_ZONE_NOTSUP: "BE_ERR_ZONE_NOTSUP",
    BE_ERR_ZONE_MPOOL_NOTSUP: "BE_ERR_ZONE_MPOOL_NOTSUP",
    BE_ERR_ZONE_RO_NOTSUP: "BE_ERR_ZONE_RO_NOTSUP",
    BE_ERR_NO_MENU_ENTRY: "BE_ERR_NO_MENU_ENTRY",
    BE_ERR_NESTED_PYTHON: "BE_ERR_NESTED_PYTHON",
    BE_ERR_PYTHON: "BE_ERR_PYTHON",
    BE_ERR_PYTHON_EXCEPT: "BE_ERR_PYTHON_EXCEPT",
    BE_ERR_RENAME_ACTIVE_ON_BOOT: "BE_ERR_RENAME_ACTIVE_ON_BOOT",
    BE_ERR_NO_RPOOLS: "BE_ERR_NO_RPOOLS",
    "BE_SUCCESS": BE_SUCCESS,
    "BE_ERR_ACCESS": BE_ERR_ACCESS,
    "BE_ERR_ACTIVATE_CURR": BE_ERR_ACTIVATE_CURR,
    "BE_ERR_AUTONAME": BE_ERR_AUTONAME,
    "BE_ERR_BE_NOENT": BE_ERR_BE_NOENT,
    "BE_ERR_BUSY": BE_ERR_BUSY,
    "BE_ERR_CANCELED": BE_ERR_CANCELED,
    "BE_ERR_CLONE": BE_ERR_CLONE,
    "BE_ERR_COPY": BE_ERR_COPY,
    "BE_ERR_CREATDS": BE_ERR_CREATDS,
    "BE_ERR_CURR_BE_NOT_FOUND": BE_ERR_CURR_BE_NOT_FOUND,
    "BE_ERR_DESTROY": BE_ERR_DESTROY,
    "BE_ERR_DEMOTE": BE_ERR_DEMOTE,
    "BE_ERR_DSTYPE": BE_ERR_DSTYPE,
    "BE_ERR_BE_EXISTS": BE_ERR_BE_EXISTS,
    "BE_ERR_INIT": BE_ERR_INIT,
    "BE_ERR_INTR": BE_ERR_INTR,
    "BE_ERR_INVAL": BE_ERR_INVAL,
    "BE_ERR_INVALPROP": BE_ERR_INVALPROP,
    "BE_ERR_INVALMOUNTPOINT": BE_ERR_INVALMOUNTPOINT,
    "BE_ERR_MOUNT": BE_ERR_MOUNT,
    "BE_ERR_MOUNTED": BE_ERR_MOUNTED,
    "BE_ERR_NAMETOOLONG": BE_ERR_NAMETOOLONG,
    "BE_ERR_NOENT": BE_ERR_NOENT,
    "BE_ERR_POOL_NOENT": BE_ERR_POOL_NOENT,
    "BE_ERR_NODEV": BE_ERR_NODEV,
    "BE_ERR_NOTMOUNTED": BE_ERR_NOTMOUNTED,
    "BE_ERR_NOMEM": BE_ERR_NOMEM,
    "BE_ERR_NONINHERIT": BE_ERR_NONINHERIT,
    "BE_ERR_NXIO": BE_ERR_NXIO,
    "BE_ERR_NOSPC": BE_ERR_NOSPC,
    "BE_ERR_NOTSUP": BE_ERR_NOTSUP,
    "BE_ERR_OPEN": BE_ERR_OPEN,
    "BE_ERR_PERM": BE_ERR_PERM,
    "BE_ERR_UNAVAIL": BE_ERR_UNAVAIL,
    "BE_ERR_PROMOTE": BE_ERR_PROMOTE,
    "BE_ERR_ROFS": BE_ERR_ROFS,
    "BE_ERR_READONLYDS": BE_ERR_READONLYDS,
    "BE_ERR_READONLYPROP": BE_ERR_READONLYPROP,
    "BE_ERR_SS_EXISTS": BE_ERR_SS_EXISTS,
    "BE_ERR_SS_NOENT": BE_ERR_SS_NOENT,
    "BE_ERR_UMOUNT": BE_ERR_UMOUNT,
    "BE_ERR_UMOUNT_CURR_BE": BE_ERR_UMOUNT_CURR_BE,
    "BE_ERR_UMOUNT_SHARED": BE_ERR_UMOUNT_SHARED,
    "BE_ERR_UNKNOWN": BE_ERR_UNKNOWN,
    "BE_ERR_ZFS": BE_ERR_ZFS,
    "BE_ERR_DESTROY_CURR_BE": BE_ERR_DESTROY_CURR_BE,
    "BE_ERR_GEN_UUID": BE_ERR_GEN_UUID,
    "BE_ERR_PARSE_UUID": BE_ERR_PARSE_UUID,
    "BE_ERR_NO_UUID": BE_ERR_NO_UUID,
    "BE_ERR_ZONE_NO_PARENTBE": BE_ERR_ZONE_NO_PARENTBE,
    "BE_ERR_ZONE_MULTIPLE_ACTIVE": BE_ERR_ZONE_MULTIPLE_ACTIVE,
    "BE_ERR_ZONE_NO_ACTIVE_ROOT": BE_ERR_ZONE_NO_ACTIVE_ROOT,
    "BE_ERR_ZONE_ROOT_NOT_LEGACY": BE_ERR_ZONE_ROOT_NOT_LEGACY,
    "BE_ERR_NO_MOUNTED_ZONE": BE_ERR_NO_MOUNTED_ZONE,
    "BE_ERR_MOUNT_ZONEROOT": BE_ERR_MOUNT_ZONEROOT,
    "BE_ERR_UMOUNT_ZONEROOT": BE_ERR_UMOUNT_ZONEROOT,
    "BE_ERR_ZONES_UNMOUNT": BE_ERR_ZONES_UNMOUNT,
    "BE_ERR_FAULT": BE_ERR_FAULT,
    "BE_ERR_RENAME_ACTIVE": BE_ERR_RENAME_ACTIVE,
    "BE_ERR_NO_MENU": BE_ERR_NO_MENU,
    "BE_ERR_DEV_BUSY": BE_ERR_DEV_BUSY,
    "BE_ERR_BAD_MENU_PATH": BE_ERR_BAD_MENU_PATH,
    "BE_ERR_ZONE_SS_EXISTS": BE_ERR_ZONE_SS_EXISTS,
    "BE_ERR_PKG_VERSION": BE_ERR_PKG_VERSION,
    "BE_ERR_ADD_SPLASH_ICT": BE_ERR_ADD_SPLASH_ICT,
    "BE_ERR_PKG": BE_ERR_PKG,
    "BE_ERR_BOOTFILE_INST": BE_ERR_BOOTFILE_INST,
    "BE_ERR_EXTCMD": BE_ERR_EXTCMD,
    "BE_ERR_ZONE_ROOT_NOT_SLASH": BE_ERR_ZONE_ROOT_NOT_SLASH,
    "BE_ERR_ZONE_NOTSUP": BE_ERR_ZONE_NOTSUP,
    "BE_ERR_ZONE_MPOOL_NOTSUP": BE_ERR_ZONE_MPOOL_NOTSUP,
    "BE_ERR_ZONE_RO_NOTSUP": BE_ERR_ZONE_RO_NOTSUP,
    "BE_ERR_NO_MENU_ENTRY": BE_ERR_NO_MENU_ENTRY,
    "BE_ERR_NESTED_PYTHON": BE_ERR_NESTED_PYTHON,
    "BE_ERR_PYTHON": BE_ERR_PYTHON,
    "BE_ERR_PYTHON_EXCEPT": BE_ERR_PYTHON_EXCEPT,
    "BE_ERR_RENAME_ACTIVE_ON_BOOT": BE_ERR_RENAME_ACTIVE_ON_BOOT,
    "BE_ERR_NO_RPOOLS": BE_ERR_NO_RPOOLS
}


BE_ATTR_ORIG_BE_NAME = "orig_be_name"
BE_ATTR_ORIG_BE_POOL = "orig_be_pool"
BE_ATTR_SNAP_NAME = "snap_name"

BE_ATTR_NEW_BE_NAME = "new_be_name"
BE_ATTR_NEW_BE_POOL = "new_be_pool"
BE_ATTR_NEW_BE_DESC = "new_be_desc"
BE_ATTR_NEW_BE_NESTED_BE = "new_be_nested_be"
BE_ATTR_NEW_BE_PARENTBE = "new_be_parentbe"
BE_ATTR_NEW_BE_ALLOW_AUTO_NAMING = "new_be_allow_auto_naming"
BE_ATTR_POLICY = "policy"
BE_ATTR_ZFS_PROPERTIES = "zfs_properties"

BE_ATTR_FS_NAMES = "fs_names"
BE_ATTR_FS_ZFS_PROPERTIES = "fs_zfs_properties"
BE_ATTR_FS_NUM = "fs_num"
BE_ATTR_SHARED_FS_NAMES = "shared_fs_names"
BE_ATTR_SHARED_FS_ZFS_PROPERTIES = "shared_fs_zfs_properties"
BE_ATTR_SHARED_FS_NUM = "shared_fs_num"

BE_ATTR_ALT_POOL = "alt_pool"
BE_ATTR_MOUNTPOINT = "mountpoint"
BE_ATTR_MOUNT_FLAGS = "mount_flags"
BE_ATTR_UNMOUNT_FLAGS = "unmount_flags"
BE_ATTR_DESTROY_FLAGS = "destroy_flags"
BE_ATTR_ROOT_DS = "root_ds"
BE_ATTR_UUID_STR = "uuid_str"

BE_ATTR_ACTIVE = "active"
BE_ATTR_ACTIVE_ON_BOOT = "active_boot"
BE_ATTR_SPACE = "space_used"
BE_ATTR_DATASET = "dataset"
BE_ATTR_STATUS = "status"
BE_ATTR_DATE = "date"
BE_ATTR_MOUNTED = "mounted"

ZFS_FS_NAMES = []
ZFS_SHARED_FS_NAMES = ["export", "export/home"]
