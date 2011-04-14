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
""" logical.py -- library containing class definitions for logical DOC objects,
including Zpool, Filesystem, and Zvol
"""

import logging
import os

from lxml import etree

from solaris_install import CalledProcessError, Popen
from solaris_install.data_object import DataObject, ParsingError
from solaris_install.data_object.simple import SimpleXmlHandlerBase
from solaris_install.logger import INSTALL_LOGGER_NAME as ILN
from solaris_install.target.size import Size
from solaris_install.target.shadow.logical import ShadowLogical
from solaris_install.target.shadow.zpool import ShadowZpool
from solaris_install.target.libbe.be import be_list, be_create, be_destroy, \
    be_activate, be_mount, be_unmount
from solaris_install.target.libbe.const import ZFS_FS_NAMES

LOFIADM = "/usr/sbin/lofiadm"
MKFILE = "/usr/sbin/mkfile"
MOUNT = "/usr/sbin/mount"
NEWFS = "/usr/sbin/newfs"
UMOUNT = "/usr/sbin/umount"
ZFS = "/usr/sbin/zfs"
ZPOOL = "/usr/sbin/zpool"


class Logical(DataObject):
    def __init__(self,  name):
        super(Logical, self).__init__(name)

        self.noswap = False
        self.nodump = False

        # shadow lists
        self._children = ShadowZpool(self)

    def to_xml(self):
        element = etree.Element("logical")
        element.set("noswap", str(self.noswap).lower())
        element.set("nodump", str(self.nodump).lower())
        return element

    @classmethod
    def can_handle(cls, element):
        """ Returns True if the element has the tag 'logical'
        """
        if element.tag == "logical":
            return True
        return False

    @classmethod
    def from_xml(cls, element):
        noswap = element.get("noswap")
        nodump = element.get("nodump")

        logical = Logical("logical")
        if noswap is not None and noswap.lower() == "true":
            logical.noswap = True
        else:
            logical.noswap = False

        if nodump is not None and nodump.lower() == "true":
            logical.nodump = True
        else:
            logical.nodump = False

        return logical

    def add_zpool(self, zpool_name, is_root=False, mountpoint=None):
        """ add_zpool() - method to create a Zpool object and add it as a child
        of the Logical object
        """
        # create a new Zpool object
        new_zpool = Zpool(zpool_name)
        new_zpool.is_root = is_root
        new_zpool.mountpoint = mountpoint

        # add the new Zpool object as a child
        self.insert_children(new_zpool)

        return new_zpool

    def delete_zpool(self, zpool):
        """ delete_zpool() - method to delete a specific Zpool object
        """
        self.delete_children(name=zpool.name, class_type=Zpool)

    def __copy__(self):
        """ method to override the parent's version of __copy__.
        We want the _children list to be a shadow list instead of a flat list
        """
        new_copy = super(Logical, self).__copy__()
        new_copy._children = ShadowZpool(self)
        return new_copy

    def __repr__(self):
        return "Logical: noswap=%s; nodump=%s" % (self.noswap, self.nodump)


class Zpool(DataObject):
    def __init__(self, name, vdev_list=None, mountpoint=None):
        super(Zpool, self).__init__(name)

        self.action = "create"
        self.is_root = False
        self.mountpoint = mountpoint

        if not isinstance(vdev_list, list):
            # convert a string of vdev(s) into a list with one element
            self.vdev_list = [vdev_list]
        else:
            self.vdev_list = vdev_list

        # shadow lists
        self._children = ShadowLogical(self)

    def to_xml(self):
        element = etree.Element("zpool")
        element.set("name", self.name)
        element.set("action", self.action)
        element.set("is_root", str(self.is_root).lower())
        if self.mountpoint is not None:
            element.set("mountpoint", self.mountpoint)

        return element

    @classmethod
    def can_handle(cls, element):
        """ Returns True if element has:
        - the tag 'zpool'
        - a name attribute

        otherwise return False
        """
        if element.tag != "zpool":
            return False
        if element.get("name") is None:
            return False
        return True

    @classmethod
    def from_xml(cls, element):
        name = element.get("name")
        action = element.get("action")
        is_root = element.get("is_root")
        mountpoint = element.get("mountpoint")

        zpool = Zpool(name)
        if action is not None:
            zpool.action = action
        if is_root is not None:
            if is_root.lower() == "true":
                zpool.is_root = True
            elif is_root.lower() == "false":
                zpool.is_root = False
            else:
                raise ParsingError("Zpool element's is_root attribute must " +
                                   "be either 'true' or 'false'")
        if mountpoint is not None:
            zpool.mountpoint = mountpoint
        return zpool

    @property
    def exists(self):
        cmd = [ZPOOL, "list", self.name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             check_result=Popen.ANY)
        return p.returncode == 0

    @property
    def filesystems(self):
        filesystem_list = []

        # if the zpool doesn't yet exist, return an empty list
        if not self.exists:
            return filesystem_list

        cmd = [ZFS, "list", "-H", "-t", "filesystem", "-o", "name", "-r",
               self.name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)
        for fs in p.stdout.splitlines():
            filesystem_list.append(Filesystem(fs))
        return filesystem_list

    def set(self, propname, propvalue, dry_run):
        cmd = [ZPOOL, "set", "%s=%s" % (propname, propvalue), self.name]
        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)

    def get(self, propname):
        cmd = [ZPOOL, "get", propname, self.name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)
        return p.stdout.strip()

    def create(self, dry_run, options=[]):
        """ method to create the zpool from the vdevs

        options - optional list of pool and/or dataset options to pass to the
        create flag
        """
        cmd = [ZPOOL, "create", "-f"]
        if options:
            cmd.extend(options)

        # add the mountpoint if specified
        if self.mountpoint is not None:
            cmd.append("-m")
            cmd.append(self.mountpoint)

        cmd.append(self.name)
        if None in self.vdev_list:
            raise RuntimeError("Invalid entry in vdev_list:  " + \
                               str(self.vdev_list))

        # add the vdev_list to the cmd to preserve the format Popen needs
        cmd += self.vdev_list

        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)

    def destroy(self, dry_run, force=False):
        """ method to destroy the zpool
        """
        if self.exists:
            cmd = [ZPOOL, "destroy"]
            if force:
                cmd.append("-f")
            cmd.append(self.name)

            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)

    def add_vdev(self, label, redundancy):
        """ add_vdev() - method to create a Vdev object and add it as a child
        of the Zpool object
        """
        # No need to validate here because the schema has a list of valid
        # redundancy values already
        new_vdev = Vdev(label)
        new_vdev.redundancy = redundancy

        self.insert_children(new_vdev)
        return new_vdev

    def delete_vdev(self, vdev):
        """ delete_vdev() - method to delete a specific Vdev object
        """
        self.delete_children(name=vdev.name, class_type=Vdev)

    def add_filesystem(self, fs_name, mountpoint=None):
        """ add_filesystem - method to create a Filesystem object and add it as
        a child of the Zpool object
        """
        # create a new Filesystem object
        new_filesystem = Filesystem(fs_name)
        new_filesystem.mountpoint = mountpoint

        # add the new Filesystem object as a child
        self.insert_children(new_filesystem)

        return new_filesystem

    def delete_filesystem(self, filesystem):
        """ delete_filesystem() - method to delete a specific Filesystem object
        """
        self.delete_children(name=filesystem.name, class_type=Filesystem)

    def add_zvol(self, zvol_name, size, size_units=Size.gb_units, use=None):
        # create a new Zvol object
        new_zvol = Zvol(zvol_name)

        # fix the size units to conform to ZFS syntax by removing the "b"
        # character from the units
        size_units = str(size_units).rstrip("bB")

        new_zvol.size = str(size) + size_units
        if use is not None:
            new_zvol.use = use

        # add the new Filesystem object as a child
        self.insert_children(new_zvol)

        return new_zvol

    def delete_zvol(self, zvol):
        """ delete_zvol() - method to delete a specific Zvol object
        """
        self.delete_children(name=zvol.name, class_type=Zvol)

    def __copy__(self):
        """ method to override the parent's version of __copy__.
        We want the _children list to be a shadow list instead of a flat list
        """
        new_copy = super(Zpool, self).__copy__()
        new_copy._children = ShadowLogical(self)
        return new_copy

    def __repr__(self):
        s = "Zpool: name=%s; action=%s" % (self.name, self.action)
        s += "; is_root=%s" % self.is_root
        if self.mountpoint is not None:
            s += "; mountpoint=%s" % self.mountpoint
        if self.vdev_list is not None:
            s += "; vdev_list=" + str(self.vdev_list)
        return s


class Vdev(DataObject):
    def __init__(self, name):
        super(Vdev, self).__init__(name)

        self.redundancy = "mirror"

    def to_xml(self):
        element = etree.Element("vdev")
        element.set("name", self.name)
        element.set("redundancy", self.redundancy)
        return element

    @classmethod
    def can_handle(cls, element):
        if element.tag == "vdev":
            return True
        return False

    @classmethod
    def from_xml(cls, element):
        name = element.get("name")
        redundancy = element.get("redundancy")
        vdev = Vdev(name)
        if redundancy is not None:
            vdev.redundancy = redundancy
        return vdev

    def __repr__(self):
        return "Vdev: name=%s; redundancy=%s" % (self.name, self.redundancy)


class Filesystem(DataObject):
    def __init__(self, name):
        super(Filesystem, self).__init__(name)

        self.action = "create"
        self.mountpoint = None
        self.zfs_options = None

        self.in_be = False

    def to_xml(self):
        element = etree.Element("filesystem")
        element.set("name", self.name)
        element.set("action", self.action)
        if self.mountpoint is not None:
            element.set("mountpoint", self.mountpoint)
        if self.zfs_options is not None:
            options = etree.SubElement(element, "options")
            options.text = self.zfs_options
        element.set("in_be", str(self.in_be).lower())

        return element

    @classmethod
    def can_handle(cls, element):
        """ Returns True if element has:
        - the tag 'filesystem'
        - a name attribute
        - an action attribute

        otherwise return False
        """
        if element.tag != "filesystem":
            return False
        if element.get("name") is None:
            return False
        return True

    @classmethod
    def from_xml(cls, element):
        name = element.get("name")
        action = element.get("action")
        mountpoint = element.get("mountpoint")
        in_be = element.get("in_be")

        filesystem = Filesystem(name)
        if action is not None:
            filesystem.action = action
        if mountpoint is not None:
            filesystem.mountpoint = mountpoint
        else:
            # Recursively strip the dataset_path until the mountpoint is
            # found.
            stripped_entries = []
            dataset_list = name.split("/")

            # add the parent's name (the zpool name) to the list
            dataset_list.insert(0, element.getparent().get("name"))

            while dataset_list:
                try:
                    test_dataset = Filesystem("/".join(dataset_list))
                    test_dataset_mp = test_dataset.get("mountpoint")
                except CalledProcessError:
                    # strip off the last element and save it for later
                    stripped_entries.append(dataset_list[-1])
                    dataset_list = dataset_list[:-1]
                    continue
                else:
                    # the mountpoint was found so add the stripped entries
                    # (in reverse) to generate the proper path.
                    filesystem.mountpoint = os.path.join(test_dataset_mp,
                        "/".join(reversed(stripped_entries)))
                    break
            else:
                # set the mountpoint to None
                filesystem.mountpoint = None

        options = element.find("options")
        if options is not None:
            filesystem.zfs_options = options.text

        if in_be is not None:
            if in_be.lower() == "true":
                filesystem.in_be = True
            elif in_be.lower() == "false":
                filesystem.in_be = False
            else:
                raise ParsingError("Filesystem element's in_be attribute " +
                                   "must be either 'true' or 'false'")

        return filesystem

    def __repr__(self):
        s = "Filesystem: name=%s; action=%s" % (self.name, self.action)
        if self.mountpoint is not None:
            s += "; mountpoint=%s" % self.mountpoint
        if self.zfs_options is not None:
            s += "; zfs_options=%s" % self.zfs_options
        return s

    def snapshot(self, snapshot_name, overwrite=False):
        snap = self.snapname(snapshot_name)
        if overwrite and snap in self.snapshot_list:
            self.destroy(dry_run=False, snapshot=snapshot_name)
        cmd = [ZFS, "snapshot", snap]
        Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                         logger=ILN)

    def snapname(self, short_name):
        '''Returns the full (dataset@snapshot) name based on the given
        short name'''
        # If this object has a parent, add the name of the parent zpool to
        # the name of the filesystem.
        if self.parent is not None:
            full_name = os.path.join(self.parent.name, self.name)
        else:
            full_name = self.name

        return full_name + "@" + short_name

    def _snapshots(self):
        ''' Get list of snapshots.  Snapshots returned will be in creation
            time order with the earliest first '''
        cmd = [ZFS, "list", "-H", "-o", "name", "-t", "snapshot",
               "-s", "creation", "-r", self.name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)
        return p.stdout.splitlines()

    # Probably a bit heavyweight for a property
    snapshot_list = property(_snapshots)

    def rollback(self, to_snapshot, recursive=False):
        cmd = [ZFS, "rollback"]
        if recursive:
            cmd.append("-r")
        cmd.append(self.snapname(to_snapshot))
        Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                         logger=ILN)

    def set(self, property, value, dry_run):
        # If this object has a parent, add the name of the parent zpool to
        # the name of the filesystem.
        if self.parent is not None:
            full_name = os.path.join(self.parent.name, self.name)
        else:
            full_name = self.name

        cmd = [ZFS, "set", "%s=%s" % (property, value), full_name]
        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)

    def get(self, property):
        # If this object has a parent, add the name of the parent zpool to
        # the name of the filesystem.
        if self.parent is not None:
            full_name = os.path.join(self.parent.name, self.name)
        else:
            full_name = self.name

        cmd = [ZFS, "get", "-H", "-o", "value", property, full_name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             stderr_loglevel=logging.DEBUG, logger=ILN)
        return p.stdout.strip()

    def create(self, dry_run):
        if not self.exists:
            cmd = [ZFS, "create", "-p"]
            if self.zfs_options is not None:
                cmd.extend(self.zfs_options.split())
            if self.mountpoint is not None:
                cmd.extend(["-o", "mountpoint=%s" % self.mountpoint])

            # If this object has a parent, add the name of the parent zpool to
            # the name of the filesystem.  If not, create the filesystem as is.
            if self.parent is not None:
                cmd.append(os.path.join(self.parent.name, self.name))
            else:
                cmd.append(self.name)

            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)

    def destroy(self, dry_run, snapshot=None, recursive=False):
        cmd = [ZFS, "destroy"]
        if recursive:
            cmd.append("-r")
        if snapshot is not None:
            cmd.append(self.snapname(snapshot))
        else:
            cmd.append(self.name)

        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)

    @property
    def exists(self):
        # If this object has a parent, add the name of the parent zpool to
        # the name of the filesystem.
        if self.parent is not None:
            full_name = os.path.join(self.parent.name, self.name)
        else:
            full_name = self.name

        cmd = [ZFS, "list", full_name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             check_result=Popen.ANY)
        return p.returncode == 0


class Zvol(DataObject):
    def __init__(self, name):
        super(Zvol, self).__init__(name)

        self.action = "create"
        self.use = "none"

        self.size = ""
        self.zfs_options = None

    def to_xml(self):
        element = etree.Element("zvol")
        element.set("name", self.name)
        element.set("action", self.action)
        element.set("use", self.use)

        size = etree.SubElement(element, "size")
        size.set("val", str(self.size))

        if self.zfs_options is not None:
            options = etree.SubElement(element, "options")
            options.text = self.zfs_options

        return element

    @classmethod
    def can_handle(cls, element):
        """ Returns True if element has:
        - the tag 'zvol'
        - a name attribute
        - an action attribute

        otherwise return False
        """
        if element.tag != "zvol":
            return False
        if element.get("name") is None:
            return False
        return True

    @classmethod
    def from_xml(cls, element):
        name = element.get("name")
        action = element.get("action")
        use = element.get("use")

        zvol = Zvol(name)

        size = element.find("size")
        if size is not None:
            if size.get("val") is None:
                raise ParsingError("Size element must contain a 'val' " + \
                                   "attribute")
            zvol.size = size.get("val")
        else:
            raise ParsingError("Zvol element must contain a size subelement")

        options = element.find("options")
        if options is not None:
            zvol.zfs_options = options.text

        zvol.action = action
        if use is not None:
            zvol.use = use
        return zvol

    @property
    def exists(self):
        cmd = [ZFS, "list", self.name]
        p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             check_result=Popen.ANY)
        return p.returncode == 0

    def create(self, dry_run):
        """ method to create a zvol.
        """
        if not self.exists:
            cmd = [ZFS, "create", "-p", "-V", self.size]
            if self.zfs_options is not None:
                cmd.extend(self.zfs_options.split())
            # If this object has a parent, add the name of the parent zpool to
            # the name of the zvol.  If not, create the zvol as is.
            if self.parent is not None:
                cmd.append(os.path.join(self.parent.name, self.name))
            else:
                cmd.append(self.name)

            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)

    def destroy(self, dry_run):
        """ method to destroy a zvol.
        """
        if self.exists:
            cmd = [ZFS, "destroy", self.name]
            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)

    def __repr__(self):
        return "Zvol: name=%s; action=%s; use=%s; size=%s" % \
            (self.name, self.action, self.use, self.size)

    def resize(self, new_size, size_units=Size.gb_units):
        """ resize() - method to resize a Zvol object.
        """
        # delete the existing Zvol from the parent's shadow list
        self.delete()

        # re-insert it with the new size
        self.parent.add_zvol(self.name, new_size, size_units)

        # reset the attributes with the proper values
        self.size = str(new_size) + str(size_units)


class Options(DataObject):
    def __init__(self, name):
        super(Options, self).__init__(name)

        self.options_str = ""

    def to_xml(self):
        element = etree.Element("options")
        element.text = self.options_str
        return element

    @classmethod
    def can_handle(cls, element):
        if element.tag == "options":
            return True
        return False

    @classmethod
    def from_xml(cls, element):
        options = Options("options")
        options.options_str = element.text

        return options


class PoolOptions(SimpleXmlHandlerBase):
    TAG_NAME = "pool_options"


class DatasetOptions(SimpleXmlHandlerBase):
    TAG_NAME = "dataset_options"


class BE(DataObject):
    def __init__(self, name="solaris"):
        super(BE, self).__init__(name)

        self.mountpoint = None

    def to_xml(self):
        element = etree.Element("be")
        element.set("name", self.name)
        return element

    @classmethod
    def can_handle(cls, element):
        """ Returns True if element has:
        - the tag 'BE'

        otherwise return False
        """
        if element.tag != "be":
            return False
        return True

    @classmethod
    def from_xml(cls, element):
        name = element.get("name")
        if name:
            be = BE(name)
        else:
            be = BE()
        return be

    @property
    def exists(self):
        exists = be_list(self.name)
        return exists

    def __repr__(self):
        s = "BE: name=%s" % self.name
        if self.mountpoint is not None:
            s += "; mountpoint=%s" % self.mountpoint
        return s

    def create(self, dry_run, pool_name="rpool", zfs_fs_list=ZFS_FS_NAMES):
        """ method to create a BE.
        """
        if not self.exists:
            if not dry_run:
                be_create(self.name, pool_name, zfs_fs_list)
                # if a mountpoint was specified, mount the freshly
                # created BE and create the mountpoint in the process
                # if it does not exist
                if self.mountpoint is not None:
                    self.mount(self.mountpoint, dry_run)

    def destroy(self, dry_run):
        """ method to destroy a BE.
        """
        if self.exists:
            if not dry_run:
                be_destroy(self.name)

    def activate(self, dry_run):
        """ method to activate a BE.
        """
        if self.exists:
            if not dry_run:
                be_activate(self.name)

    def mount(self, mountpoint, dry_run):
        """ method to mount a BE.
        """
        if self.exists:
            if not dry_run:
                if not os.path.exists(mountpoint):
                    os.makedirs(mountpoint)
                be_mount(self.name, mountpoint)
                self.mountpoint = mountpoint

    def unmount(self, dry_run):
        """ method to unmount a BE.
        """
        if self.exists:
            if not dry_run:
                be_unmount(self.name)
                self.mountpoint = None


class Lofi(object):
    """ class representing a loopback file interface. The backing-store does
    not have to exist; it can be created
    """
    def __init__(self, ramdisk, mountpoint, size=0):
        """ constructor for the class

        ramdisk - path to the file to use as a backing store
        mountpoint - path to mount the file as a loopback device
        size - size of the file to create
        """
        self.ramdisk = ramdisk
        self.mountpoint = mountpoint
        self.size = size

        self.lofi_device = None
        self._mounted = False

        self.nbpi = None

    @property
    def exists(self):
        return os.path.exists(self.ramdisk)

    @property
    def mounted(self):
        return self._mounted

    @mounted.setter
    def mounted(self, val):
        self._mounted = val

    def create_ramdisk(self, dry_run):
        if not self.exists:
            # create the file first
            cmd = [MKFILE, "%dk" % self.size, self.ramdisk]

            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)

    def create(self, dry_run):
        # create the ramdisk (if needed)
        self.create_ramdisk(dry_run)

        # create the lofi device
        cmd = [LOFIADM, "-a", self.ramdisk]
        if not dry_run:
            p = Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)
            self.lofi_device = p.stdout.strip()

        # newfs it
        cmd = [NEWFS, "-m", "0", "-o", "space"]
        if self.nbpi is not None:
            cmd.append("-i")
            cmd.append(str(self.nbpi))
        cmd.append(self.lofi_device.replace("lofi", "rlofi"))
        if not dry_run:
            # due to the way Popen works, we can not assign a logger to the
            # call, otherwise the process will complete before we can pass the
            # "y" to newfs
            logger = logging.getLogger(ILN)
            logger.debug("Executing: %s" % " ".join(cmd))
            p = Popen(cmd, stdin=Popen.PIPE, stdout=Popen.DEVNULL,
                      stderr=Popen.DEVNULL)
            p.communicate("y\n")

        # ensure a directory exists to mount the lofi device to
        if not os.path.exists(self.mountpoint) and not dry_run:
            os.makedirs(self.mountpoint)

        cmd = [MOUNT, "-F", "ufs", "-o", "rw", self.lofi_device,
               self.mountpoint]
        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)
        self.mounted = True

    def unmount(self, dry_run):
        """ method to unmount the ramdisk
        """
        if self.mounted:
            cmd = [UMOUNT, "-f", self.mountpoint]
            if not dry_run:
                Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                                 logger=ILN)
            self.mounted = False

    def destroy(self, dry_run):
        """ method to unmount and destroy the lofi device
        """
        if not self.exists:
            return

        self.unmount(dry_run)

        # there is an undocumented -f flag to lofiadm to 'force' the destroy
        cmd = [LOFIADM, "-f", "-d", self.lofi_device]
        if not dry_run:
            Popen.check_call(cmd, stdout=Popen.STORE, stderr=Popen.STORE,
                             logger=ILN)

    def __repr__(self):
        return "Lofi: ramdisk=%s; mountpoint=%s; size=%s; lofi_device=%s; " \
               "mounted=%s; nbpi=%s" % \
               (self.name, self.mountpoint, self.size, self.lofi_device,
               self._mounted, self.nbpi)