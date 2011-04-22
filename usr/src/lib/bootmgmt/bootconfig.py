#! /usr/bin/python2.6
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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

"""
Abstractions for boot configuration management.  A BootConfig object aggregates
the boot configuration for a system.  Implementations are provided for active
systems (systems that boot from disk devices) as well as for installation image
creation (i.e. for network-based installation images, optical disc installation
images, and USB-based installation images).
BootConfig provides access, through abstractions, to the system boot loader,
general system boot configuration variables, and the set of bootable instances
that the boot loader provides a means to boot.
"""

import collections
import gettext
import unittest

from . import BootmgmtNotSupportedError, BootmgmtArgumentError
from . import BootmgmtMissingInfoError
from . import bootinfo, bootloader
from .bootutil import LoggerMixin, get_current_arch_string

_ = gettext.translation("SUNW_OST_OSCMD", "/usr/lib/locale",
    fallback=True).gettext

class BootConfig(LoggerMixin):
    """Abstract base class for boot configuration classes"""

    # Valid flags to pass to __init__:
    (BCF_CREATE,
     BCF_ONESHOT,
     BCF_AUTOGEN,
     BCF_MIGRATE) = range(4)

    BOOT_CLASS_DISK = 'disk'
    BOOT_CLASS_NET = 'net'
    BOOT_CLASS_ODD = 'odd'

    TOKEN_SYSTEMROOT = 'systemroot'

    OUTPUT_TYPE_FILE = 'file'
    OUTPUT_TYPE_BIOS_ELTORITO = 'bios-eltorito-image'

    # Tuple indices for each tuple in the list output by the install() method
    (IDX_FILETYPE,
     IDX_TEMPNAME,
     IDX_INSTANCE,
     IDX_DESTNAME,
     IDX_USER,
     IDX_GROUP,
     IDX_MODE) = range(7)

    @property
    def dirty(self):
        return self._dirty

    @dirty.setter
    def dirty(self, val):
        if not self._dirty and val is True:
            self._debug('dirty set')
            self._dirty = True
        else:
            self._dirty = val

    def __init__(self, flags, **kwargs):
        """
        Initialize the basic set of attributes for all BootConfig classes
        -----------------------------------------------------------------
        Argument|  Valid Value(s)
        --------+--------------------------------------------------------
        flags   | <Tuple of flags that modify the behavior of this
                |  BootConfig object> [tuple]
                |
                | Allowed values in the tuple:
                | ----------------------------
                | BootConfig.BCF_CREATE: Create a new boot
                |                        configuration.  If one already 
                |                        exists, it will be discarded.
                |                        If this flag is not present and
                |                        no boot configuration exists,
                |                        an exception
                |                        (BootmgmtConfigurationReadError)
                |                        will be raised.
                | BootConfig.BCF_ONESHOT:If True, only the final boot
                |                        configuration files will be
                |                        created when the
                |                        commit_boot_config() is called.
                |                        This is useful for one-shot
                |                        creation of boot configurations
                |                        for installation media.
                | BootConfig.BCF_AUTOGEN:The set of boot instances will be
                |                        automatically generated by
                |                        scanning the system.  The list of
                |                        boot instances found by the scan
                |                        will be available after
                |                        the BootConfig object is created.
                |                        This set of boot instances can
                |                        then be tailored before calling
                |                        commit_boot_config().  Note that
                |                        the set of BootInstance objects
                |                        added to this object's boot_instances
                |                        are only guaranteed to include
                |                        Solaris boot instances.
                | BootConfig.BCF_MIGRATE:If supported, the existing boot
                |                        configuration will be migrated
                |                        from an older form to a newer
                |                        form (i.e. conversion from legacy
                |                        GRUB's menu.lst to GRUB2's
                |                        configuration format). The conversion
                |                        is performed at commit_boot_config()
                |                        time.  Cannot be used with the
                |                        BCF_CREATE flag.
                |
        platform| [optional] The target system architecture / firmware for
        (keyword| which this boot configuration will be written.  Useful
         arg)   | for the creation of boot configuration files for network
                | boot instances, or when a boot configuration is created
                | on a system of a different architecture / firmware than
                | the target.  If this is not supplied, the assumption is
                | that the boot configuration will be for the current
                | system.  Supported tuple values are:
                | { ('sparc', 'obp' (or None)),
                |   ('x86', 'bios' (or None)),
                |   ('x86', 'uefi64') }
                | If a particular BootConfig subclass does not support  
                | manipulation of a different platform's boot
                | configuration, a BootmgmtNotSupportedError will be
                | raised.
        -----------------------------------------------------------------
        """

        self.boot_instances = []
        self.boot_class = kwargs.get('boot_class', None)
        self.boot_fstype = kwargs.get('boot_fstype', None)
        self._platform = kwargs.get('platform', None)
        self._dirty = False
        self._flags = flags
        self.boot_loader = self._get_boot_loader(**kwargs)

        if (BootConfig.BCF_CREATE in self._flags and
            BootConfig.BCF_MIGRATE in self._flags):
            raise BootmgmtUnsupportedOperationError(
                  'Migration cannot be combined with creation')

        if BootConfig.BCF_CREATE in self._flags:
            self._new_boot_config(**kwargs)
        else:
            # _load_boot_config raises BootmgmtConfigurationReadError
            self._load_boot_config(**kwargs)

        if BootConfig.BCF_AUTOGEN in self._flags:
            self._autogenerate_config(**kwargs)

    def _autogenerate_config(self, **kwargs):
        from .backend.autogen import BootInstanceAutogenFactory
        inst_list = BootInstanceAutogenFactory.autogen(self)
        if len(inst_list) > 0:
            self.boot_instances += inst_list
            self.dirty = True

    def _load_boot_config(self, **kwargs):
        """[Implemented by child classes] Loads the boot configuration (with
        guidance from kwargs)."""
        pass

    def _new_boot_config(self, **kwargs):
        """[Implemented by child classes] Initializes this instance with
        a new boot configuration (with guidance from kwargs)."""
        pass

    def _get_boot_loader(self, **kwargs):
        """Initializes this instance's boot_loader"""
        loader = bootloader.BootLoader.get(bootconfig=self)
        self._debug('loader = ' + str(loader))
        return loader

    def get_root(self):
        "Return the root directory where this BootConfig is be stored"
        return None

    def add_boot_instance(self, boot_instances, where=-1):
        """
        Adds one or more new boot instances to this boot configuration.
        boot_instance is a list of BootInstance references or a single
        BootInstance reference.
        The where argument determines where in the ordered list of boot
        instances this boot instance entry will be placed.  The default
        is -1, which means the end of the list of boot instances.  If
        where is a callable object, it will be used to iterate through
        the boot instances list.  If it returns a negative value, the
        boot_instance will be inserted BEFORE that item;  If it returns
        a positive value, the boot_instance will be inserted AFTER that
        item;  If it returns 0, no insertion will occur at that point
        in the iteration.  The arguments to the function are
        (<current_boot_instance_in_the_iteration>, boot_instance).
        """

        if isinstance(boot_instances, collections.Iterable):
            for instance in boot_instances:
                self._add_one_boot_instance(instance, where)
                # If where is not -1, make sure we increment it so that
                # subsequent boot instances are placed in the boot_instances
                # array in the proper order
                if where > -1:
                    where = where + 1
        else:
            self._add_one_boot_instance(boot_instances, where)


    def _add_one_boot_instance(self, boot_instance, where=-1):

        prevDefault = None

        if boot_instance.default is True:
            # Find previous default (if any)
            defaults = [x for x in self.boot_instances
                        if x.default is True]
            if len(defaults) > 0:
                # XXX - assert here that len(defaults) is 1?
                prevDefault = defaults[0]
                self._debug("Previous default was:\n%s" % str(prevDefault))

        if type(where) is int:
            # where is an index if it's > 0
            where = where if where >= 0 else len(self.boot_instances)
            self.boot_instances[where:where] = [boot_instance]
            boot_instance._bootconfig = self
            self.dirty = True
        elif callable(where):
            for index, inst in enumerate(self.boot_instances):
                whereval = where(inst, boot_instance)
                if whereval < 0:
                    self.boot_instances[index:index] = [boot_instance]
                    boot_instance._bootconfig = self
                    self.dirty = True
                    break
                elif whereval > 0:
                    self.boot_instances[index + 1: index + 1] = [boot_instance]
                    boot_instance._bootconfig = self
                    self.dirty = True
                    break
        else:
            raise BootmgmtArgumentError('The where parameter is malformed')

        if not prevDefault is None:
            prevDefault.default = False

    def delete_boot_instance(self, filter_func, all=True):
        """
        Deletes one or more boot instances from the boot configuration.
        filter_func is a function that takes a single argument (a
        BootInstance).  If it returns True, that BootInstance is removed
        from the boot configuration.
        The 'all' parameter is True if all matching boot instances are
        to be deleted.  If False, only the first instance for which
        filter_func returns True is deleted.
           For example:
              delete_boot_instance(lambda x: x.title() == 'snv_158')
           will delete all boot instances whose titles match 'snv_158'.
        """
        if all is True:
            oldlen = len(self.boot_instances)
            self.boot_instances = filter(lambda x: not filter_func(x),
                                         self.boot_instances)
            if oldlen != len(self.boot_instances):
                self.dirty = True
        else:
            for idx, inst in enumerate(self.boot_instances):
                if filter_func(inst) is True:
                    del self.boot_instances[idx]
                    self.dirty = True
                    return

    def modify_boot_instance(self, filter_func, mod_func):
        """
        Applies mod_func to all boot instances for which filter_func
        returns True.
        This is shorthand for:

          for bi in filter(filter_func, bootconfig_obj.boot_instances):
              mod_func(bi)

        (The (single) argument to filter_func and mod_func is a
        BootInstance ref).
        """
        for inst in filter(filter_func, self.boot_instances):
            mod_func(inst)

    def commit_boot_config(self, temp_dir=None, boot_devices=None):
        """Writes the boot configuration (including boot instances and boot
        loader settings) to stable storage.

        If this object was created with the BootConfig.BC_ONESHOT flag,
        then only the final boot configuration file(s) will be created
        (i.e. the Legacy GRUB menu.lst, GRUB2 configuration file, or
        SPARC menu.lst)-- any other state files that store boot
        configuration information will not be written (customizations to
        this boot configuration may not persist).  This may prevent
        incremental modifications to the boot configuration from
        being possible (depending on the boot loader in use).  BC_ONESHOT
        should only be used when creating a boot configuration that is
        not intended to change (i.e. when creating a boot configuration
        for use with an install image, not a running system).

        boot_devices is a list of strings, each of which is the path
        to a character-special device where the boot loader should be
        installed.  This argument should be omitted when the consumer
        desires to write the boot configuration files to a temporary
        directory.

        If temp_dir is not None, then the set of files that constitute
        the boot configuration is written to a temporary directory (and
        not to the official location(s)).  (temp_dir must exist or an  
        IOError will be raised).  When files are written to temp_dir,   
        commit_boot_config() returns a list of 7-tuples of the following
        form:
         (<type>, <srcpath>, <object>, <destpath>, <uid>, <gid>, <mode>)
        Each of these tuples describes a boot configuration file that
        was written, along with the required system-relative path where
        it must be copied.  This enables a consumer to install the
        file(s) into the correct place(s) on a system or install image
        without having to hard-code knowledge of the filenames)).
        For example:
        [('file', '/tmp/bc.Jzx1cZa/boot/solaris/bootenv.rc',
          <bootmgmt.bootconfig.SolarisDiskBootInstance object at ...>,
          '%(systemroot)s/boot/solaris/bootenv.rc', 'root', 'sys', 0644)]

        The <type> value in the tuple identifies the type of file in
        the tuple.  This is useful for conveying platform-specific
        attributes of a particular file.  For example, <type> could be
        'eltorito' to identify an eltorito boot image, which a consumer
        would recognize and then use to supply the argument to
        `mkisofs -b`.  See child class definitions for additional <type>
        values.  Only <type>='file' is defined at the BootConfig level.

        The <object> element, if not None, is a reference to a
        BootInstance object.  It provides clarification for tokens that
        are embedded in the destination path.  In the above example, the
        `systemroot' token refers to the root path of the Solaris BE
        identified by the SolarisDiskBootInstance object in the tuple.
        In this example, including the object allows the consumer to
        resolve the root path location.

        Note the use of Python string-formatting tokens, for which a
        consumer must supply definitions.  The following list of tokens
        are defined at the BootConfig level; child classes may define
        additional tokens:
        ----------------------------------------------------------------
        Token Name        |  Meaning
        ------------------+---------------------------------------------
        systemroot        | The path to a boot instance's mounted root
                          | filesystem. [string]
        ----------------------------------------------------------------
        """
        if temp_dir is None:
            if self.dirty is True:
                for inst in self.boot_instances:
                    if (not inst.boot_vars is None and
                       inst.boot_vars.dirty is True):
                        inst.boot_vars.write(inst=inst)

            if self.dirty is True or self.boot_loader.dirty is True:
                self.boot_loader.install(boot_devices)

            return None
        else:
            tuple_list = []
            if self.dirty is True:
                for inst in self.boot_instances:
                    if (not inst.boot_vars is None and
                       inst.boot_vars.dirty is True):
                        tuple_list.append(inst.boot_vars.write(inst, temp_dir))

            if self.dirty is True or self.boot_loader.dirty is True:
                tuple_list.append(self.boot_loader.install(temp_dir))

            return tuple_list


    def __str__(self):
        s = 'State: ' + ('dirty' if self.dirty else 'clean') + '\n'
        s += 'Class: ' + (self.boot_class
                          if not self.boot_class is None else 'none') + '\n'
        s += 'FSType: ' + ((self.boot_fstype
                           if not self.boot_fstype is None else 'unknown') +
                          '\n')
        s += 'Boot instances: ' + str(len(self.boot_instances)) + '\n'
        for idx, inst in enumerate(self.boot_instances):
            s += '===[ Instance ' + str(idx) + ' ]===\n'
            s += '\t' + str(inst).replace('\n', '\n\t') + '\n'
        s += '===[ End Boot Instances ]===\n'
        if not self.boot_loader is None:
            s += 'Boot loader:\n'
            s += str(self.boot_loader) + '\n'
        else:
            s += 'No boot loader'
        return s


class DiskBootConfig(BootConfig):
    """A class for managing the boot configuration stored on hard disk-like
    storage devices.  Handles Solaris boot configurations stored in ZFS root
    pools and on devices with a UFS root filesystem"""

    # Key names for keyword arguments to the constructor
    ARG_ZFS_RPNAME = 'rpname'         # ZFS root pool name
    ARG_ZFS_TLDPATH = 'tldpath'       # ZFS top-level dataset mounted path
    ARG_ZFS_SYSROOT_PATH = 'zfspath'  # ZFS system root mounted path
    ARG_UFS_ROOT = 'ufsroot'          # UFS root mounted path
    
    # Tokens returned from commit_boot_config()
    TOKEN_ZFS_RPOOL_TOP_DATASET = 'rpool_top_dataset'

    def __init__(self, flags, **kwargs):
        # DiskBootConfig does not support platforms other than native
        platform = kwargs.get('platform', None)
        if not platform is None:
            raise BootmgmtNotSupportedError(self.__class__.__name__ + ' does '
                  'not support cross-platform operations')

        fstype = None

        if (DiskBootConfig.ARG_ZFS_RPNAME in kwargs and
            DiskBootConfig.ARG_ZFS_TLDPATH in kwargs and
            DiskBootConfig.ARG_ZFS_SYSROOT_PATH in kwargs):
            fstype = 'zfs'
            self.sysroot = kwargs[DiskBootConfig.ARG_ZFS_SYSROOT_PATH]
            self.zfstop = kwargs[DiskBootConfig.ARG_ZFS_TLDPATH]
            self.zfsrp = kwargs[DiskBootConfig.ARG_ZFS_RPNAME]
        elif DiskBootConfig.ARG_UFS_ROOT in kwargs:
            fstype = 'ufs'
            self.sysroot = kwargs[DiskBootConfig.ARG_UFS_ROOT]

        if fstype is None:
            raise BootmgmtNotSupportedError('The filesystem type supplied '
                'to the DiskBootConfig constructor was not recognized')

        super(self.__class__, self).__init__(flags,
                                    boot_class=BootConfig.BOOT_CLASS_DISK,
                                    boot_fstype=fstype, **kwargs)
    def get_root(self):
        return self.sysroot

    def _load_boot_config(self, **kwargs):
        """Loads the boot configuration"""
        self.boot_loader.load_config()

    def _new_boot_config(self, **kwargs):
        """Initializes this instance with a new boot configuration"""
        self.boot_loader.new_config()


class ODDBootConfig(BootConfig):
    """A class for managing the boot configuration stored on optical disk
    storage devices.  Handles Solaris boot configurations stored on DVD
    media"""

    def __init__(self, flags, **kwargs):
        ""
        # Weed out unsupported flags:
        if BootConfig.BCF_MIGRATE in flags:
            raise BootmgmtUnsupportedOperationError(self.__class__.__name__ +
                                                    ': Migration is not supported')

        # Save the image's root directory:
        self.odd_image_root = kwargs.get('oddimage_root', None)
        if self.odd_image_root is None:
            raise BootmgmtArgumentError('Missing oddimage_root argument')

        super(self.__class__, self).__init__(flags,
                                    boot_class=BootConfig.BOOT_CLASS_ODD,
                                    **kwargs)

    def get_root(self):
        return self.odd_image_root



###############################################################################
    

class BootInstance(LoggerMixin):
    """BootInstance is the core abstraction for a bootable instance of an
    operating system.  BootConfig objects aggregate zero or more
    BootInstance objects.  A BootInstance should always be associated
    with a BootConfig object (so that if a consumer modifies a
    BootInstance, the BootConfig is notified that its state has
    changed so it can write updated configuration information at
    BootConfig.commit_boot_config()-time).  This association is made
    when a BootInstance is passed to BootConfig.add_boot_instance()."""

    # Valid attributes ('default' is a special case)
    _attributes = { 'title' : None }

    def __init__(self, rootpath, **kwargs):
        # If the child class added its own set of attributes, just append to
        # it; overwise, set it to the default set from this class
        (self.__dict__.setdefault('_attributes', {})
                     .update(BootInstance._attributes))

        self._bootconfig = None
        self._default = False
        self.boot_vars = None
        self.rootpath = None

        # Only add attributes to this instance if they were not overridden
        # by the caller's keyword arguments
        for key, value in self._attributes.items():
            # If the key wasn't already set in this instance (in
            # init_from_rootpath), init it to a default value here
            if not key in kwargs and not key in self.__dict__:
               self._debug('DEFAULT: Setting %s="%s"' %
                            (str(key), str(value)))
               self.__setattr__(key, value)

        # If the user passed in keyword args, add them as attributes
        for key, value in kwargs.items():
            # If the key wasn't already set in this instance (in
            # init_from_rootpath), init it to a default value here
            if key in self.__dict__:
                continue
            self._debug('KWARGS: Setting %s="%s"' % (str(key), str(value)))
            self.__setattr__(key, value)

        # init_from_rootpath must be called after all instance variables
        # have been set
        if not rootpath is None:
            self.init_from_rootpath(rootpath)

    @property
    def default(self):
        return self._default

    def init_from_rootpath(self, rootpath):
        """Initialize the boot instance with information from the root path
        given."""
        self.rootpath = rootpath
        self.boot_vars = self._get_boot_vars(rootpath)

    def _set_default(self, value):
        """By setting this BootInstance to be the default, the previous default
        must be disabled.  This property setter function does just that by
        using the _bootconfig reference to find the list of boot instances,
        locating the previous default, and setting it to False.  Note that
        only setting the default to True triggers this search (otherwise, we
        could end up in an infinite recursive loop)."""

        if not value is False and not value is True:
            raise BootmgmtArgumentError('default must be either True or False')

        if self._default == value: # Nothing to do
            return

        if value is False:
            self._default = False
        else:
            self._default = True

            if not self._bootconfig is None:
                for inst in self._bootconfig.boot_instances:
                    if not inst is self and inst.default is True:
                        self._debug('Found previous default -- clearing it')
                        # The following assignment's side-effect marks the
                        # BootConfig dirty
                        inst.default = False
                        return

        if not self._bootconfig is None:
            # If we got here, then either the value is False or it was True
            # and no previous default was found, so mark the BootConfig dirty
            self._bootconfig.dirty = True

    def _get_boot_vars(self, rootpath):
        """Initializes this instance's boot_vars"""
        return bootinfo.BootVariables.get(sysroot=rootpath)

    def __setattr__(self, key, value):
        """Intercept the set attribute method to enforce setting of a particular
        set of boot instance attributes (the union of those defined in child
        classes and this class).  'default' is treated specially (see
        _set_default())"""

        if key == 'default':
            self._debug('key="%s" value="%s"' % (key, value))
            return self._set_default(value)

        self.__dict__[key] = value
        if key in self._attributes and not self._bootconfig is None:
            self._debug('key="%s" value="%s"' % (key, value))
            if not self._bootconfig is None:
                self._bootconfig.dirty = True

    def __delattr__(self, key):
        """Delete an attribute from this BootInstance.  'default' is treated
        specially, as with __setattr__."""

        if key == 'default':
            self._debug('delete key="default"')
            self._set_default(False)  # Same effect as setting it to False
            return

        if not key in self.__dict__:
            raise AttributeError(str(key))

        del self.__dict__[key]

        if key in self._attributes and not self._bootconfig is None:
            self._debug('delete key="%s"' % key)
            if not self._bootconfig is None:
                self._bootconfig.dirty = True

    def __str__(self):
        s = ''
        if self.default is True:    # only print default if it's True
            s += 'default = True\n'
        for item in self._attributes.keys():
            s += str(item) + ' = '
            s += str(self.__dict__.get(item, '<Not Defined>')) + '\n'
        if not self.boot_vars is None:
            s += '===[ %d Boot variables ]===\n' % len(self.boot_vars)
            s += '\n'.join(map(str, self.boot_vars))
            s += '\n===[ End Boot Variables ]==='
        return s


class ChainDiskBootInstance(BootInstance):
    """A boot instance of a chainloaded operating system"""

    _attributes = { 'chaininfo'  : None,
                    'chainstart' : '0',
                    'chaincount' : '1' }

    def __init__(self, rootpath=None, **kwargs):
        """rootpath is not supported, so should remain `None'"""
        (self.__dict__.setdefault('_attributes', {})
                      .update(ChainDiskBootInstance._attributes))
        super(ChainDiskBootInstance, self).__init__(None, **kwargs)


class SolarisBootInstance(BootInstance):
    """Abstraction for a Solaris Boot Instance.  Supported attributes are:
               - kernel [string] [optional]
               - boot_archive [string] [optional]
               - kargs [string] [optional]: Kernel argument string
               - signature [string] [optional]: The "boot signature" of this
                                                boot instance.
    """

    if get_current_arch_string() == 'x86':
        _attributes = {
                      'kernel' : '/platform/i86pc/kernel/%(karch)s/unix',
                      'boot_archive' : '/platform/i86pc/%(karch)s/boot_archive',
                      'kargs' : None,
                      'signature' : None,
                      }
    else:
        _attributes = {}


    def __init__(self, rootpath, **kwargs):
        # If the child class added its own set of attributes, just append to
        # it; overwise, set it to the default set from this class
        (self.__dict__.setdefault('_attributes', {})
                      .update(SolarisBootInstance._attributes))

        super(SolarisBootInstance, self).__init__(rootpath, **kwargs)        


class SolarisDiskBootInstance(SolarisBootInstance):
    """Abstraction for a Disk-based Solaris Boot Instance.  Additional
       attributes supported are:
               - fstype [string] [required]: One of: [ 'ufs', 'zfs' ]
               - If fstype == 'zfs':
                 * bootfs [string] [required]
    """
    _attributes = { 'fstype' : 'zfs',
                    'bootfs' : None }

    def __init__(self, rootpath, **kwargs):
        # If the child class added its own set of attributes, just append to
        # it; overwise, set it to the default set from this class
        (self.__dict__.setdefault('_attributes', {})
                      .update(SolarisDiskBootInstance._attributes))

        super(SolarisDiskBootInstance, self).__init__(rootpath, **kwargs)

        if self.fstype == 'zfs':
            if not 'bootfs' in kwargs:
                raise BootmgmtMissingInfoError('missing bootfs arg')
            # Make sure bootfs appears to be well-formed
            bootfs_spec = kwargs['bootfs'].split('/', 2)
            if len(bootfs_spec) != 3 or bootfs_spec[1] != 'ROOT':
                raise BootmgmtArgumentError('Invalid bootfs: %s' %
                                            kwargs['bootfs'])

        # If title is STILL None, try an alternate (the last component of the
        # bootfs):
        try:
            self.title = self.bootfs.split('/', 2)[2]
        except:
            pass

    def init_from_rootpath(self, rootpath):
        # Invoke the parent's init_from_rootpath first
        super(SolarisBootInstance, self).init_from_rootpath(rootpath)

        # self.title is guaranteed to have been initialized to something
        if not self.title is None:
            return    # No need to get the title from /etc/release

        try:
            alt_title = self.bootfs.split('/', 2)[2]
        except:
            alt_title = None

        self.title = solaris_release_name(rootpath, alt_title)


class SolarisNetBootInstance(SolarisBootInstance):
    """Abstraction for a Network-based Solaris Boot Instance.
    """
    def __init__(self, rootpath, **kwargs):
        super(SolarisNetBootInstance, self).__init__(rootpath, **kwargs)        


class SolarisODDBootInstance(SolarisBootInstance):
    """Abstraction for an optical-disc-based Solaris Boot Instance
    """
    def __init__(self, rootpath, **kwargs):
        super(SolarisODDBootInstance, self).__init__(rootpath, **kwargs)

    def init_from_rootpath(self, rootpath):
        # Invoke the parent's init_from_rootpath first
        super(SolarisBootInstance, self).init_from_rootpath(rootpath)

        # self.title is guaranteed to have been initialized to something
        if not self.title is None:
            return

        self.title = solaris_release_name(rootpath, None)


def solaris_release_name(rootpath, alt_title):
    # On a disk-based instance, the title can be derived from the
    # first line of the /etc/release file, if it exists
    title = alt_title if not alt_title is None else 'Oracle Solaris'
    try:
        with open(rootpath + '/etc/release') as etcrelease:
            title = etcrelease.readline().strip()
    except IOError:
        pass

    return title


###############################################################################
####################################  TESTS  ##################################
###############################################################################

class TestBootConfig(unittest.TestCase):

    def setUp(self):
        pass

    def tearDown(self):
        print 'Done with ' + str(self)

    def test_flags(self):
        pass

def testSuite():
    return unittest.TestLoader().loadTestsFromTestCase(TestBootConfig)

if __name__ == '__main__':
    unittest.main()
