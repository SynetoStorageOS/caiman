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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

""" test_pre_pkg_img_mod

 Test program for pre_pkg_img_mod

"""

import os
import os.path
import tempfile
import shutil
import subprocess
import time
import unittest

import testlib

from osol_install.install_utils import dir_size
from solaris_install.data_object.data_dict import DataObjectDict
from solaris_install.distro_const import DC_LABEL, DC_PERS_LABEL
from solaris_install.distro_const.checkpoints.pre_pkg_img_mod \
    import PrePkgImgMod, AIPrePkgImgMod, LiveCDPrePkgImgMod
from solaris_install.engine import InstallEngine

_NULL = open("/dev/null", "r+")

NODENAME = """  <service name="system/identity" type="service" version="1">
    <exec_method name="start" type="method" exec=":true" timeout_seconds="60"/>
    <exec_method name="stop" type="method" exec=":true" timeout_seconds="60"/>
    <instance name='node' enabled='true'>
      <property_group name='config' type='application'>
        <propval name='nodename' type='astring' value='solaris'/>
      </property_group>
    </instance>
  </service>"""


class TestSetPassword(unittest.TestCase):
    """ test case to test setting the password
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.pi_filelist = ["/etc/passwd", "/etc/shadow", "/etc/security/"]
        self.ppim = PrePkgImgMod("Test PPIM")
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

        # copy /etc/security/policy.conf to the pkg_img_path
        shutil.copy2("/etc/security/policy.conf",
                     os.path.join(self.ppim.pkg_img_path,
                                  "etc/security/policy.conf"))

        # copy /etc/passwd and shadow to the pkg_img_path
        shutil.copy2("/etc/passwd", os.path.join(self.ppim.pkg_img_path,
                                                 "etc/passwd"))
        shutil.copy2("/etc/shadow", os.path.join(self.ppim.pkg_img_path,
                                                 "etc/shadow"))

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_cleartext_password(self):
        self.ppim.root_password = "test"
        self.ppim.is_plaintext = "true"
        self.ppim.set_password()

        with open(os.path.join(self.ppim.pkg_img_path, "etc/shadow")) as fh:
            line = fh.readline()
            if line.startswith("root"):
                self.assert_(self.ppim.root_password not in line)

    def test_encrypted_password(self):
        self.ppim.root_password = "test"
        self.ppim.is_plaintext = "false"
        self.ppim.set_password()

        with open(os.path.join(self.ppim.pkg_img_path, "etc/shadow")) as fh:
            line = fh.readline()
            if line.startswith("root"):
                self.assert_(self.ppim.root_password in line)


class TestEtcSystemModification(unittest.TestCase):
    """ test case for testing the modification of /etc/system
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.ppim = PrePkgImgMod("Test PPIM")
        self.pi_filelist = ["/etc/system"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_modify_etc_system(self):
        # test with a missing /etc/system file
        self.ppim.modify_etc_system()

        # verify the save directory entry exists
        self.assert_(os.path.isdir(os.path.join(self.ppim.pkg_img_path,
                                                "save/etc")))

        # verify the 'saved' /etc/system file is 0 bytes
        self.assert_(os.path.getsize(os.path.join(self.ppim.pkg_img_path,
                                                  "save/etc/system")) == 0)

        # verify the new entries in /etc/system
        entry_list = ["zfs:zfs_arc_max=0x4002000", "zfs:zfs_vdev_cache_size=0"]
        for entry in entry_list:
            cmd = ["/usr/bin/grep", entry, os.path.join(self.ppim.pkg_img_path,
                                                        "etc/system")]
            self.assert_(subprocess.call(cmd, stdout=_NULL, stderr=_NULL) == 0)


class TestCalculateSize(unittest.TestCase):
    """ test case for testing the calculation of the size of pkg_image_path
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.ppim = PrePkgImgMod("Test PPIM")
        self.pi_filelist = ["/etc/system"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)
        self.ppim.img_info_path = os.path.join(self.ppim.pkg_img_path, ".image_info")

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_calculate_size(self):
        # use mkfile to make a 1 MB file
        f = os.path.join(self.ppim.pkg_img_path, "foo")
        cmd = ["/usr/sbin/mkfile", "1m", f]
        subprocess.check_call(cmd, stdout=_NULL, stderr=_NULL)

        self.ppim.calculate_size()
        # subract 1 due to the size of .image_info
        by_hand = int(round((dir_size(self.ppim.pkg_img_path) / 1024))) - 1
        with open(os.path.join(self.ppim.pkg_img_path, ".image_info")) as fh:
            line = fh.readline()
            if line.startswith("IMAGE_SIZE"):
                self.assert_(str(by_hand) in line, "%d %s" % (by_hand, line))


class TestSymlinkVi(unittest.TestCase):
    """ test case for testing the symlink of vi to /usr/has/bin/vi
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.ppim = AIPrePkgImgMod("Test PPIM")
        self.pi_filelist = ["/usr/bin/vim", "/usr/has/bin/vi"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_nonexisting_symlink_with_has_bin_vi(self):
        # remove vim first
        os.remove(os.path.join(self.ppim.pkg_img_path, "usr/bin/vim"))

        self.ppim.symlink_vi()

        # verify that /usr/bin/vi is a symlink to /usr/has/bin/vi
        self.assert_(os.path.islink(os.path.join(self.ppim.pkg_img_path,
                                                 "usr/bin/vi")))

        vi_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/bin/vi"))
        hasbinvi_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/has/bin/vi"))

        self.assert_(vi_statinfo.st_ino == hasbinvi_statinfo.st_ino)

    def test_nonexisting_symlink_with_vim(self):
        self.ppim.symlink_vi()

        # verify that /usr/bin/vi is a symlink to /usr/has/bin/vi
        self.assert_(os.path.islink(os.path.join(self.ppim.pkg_img_path,
                                                 "usr/bin/vi")))

        vi_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/bin/vi"))
        vim_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/bin/vim"))
        self.assert_(vi_statinfo.st_ino == vim_statinfo.st_ino)

    def test_existing_symlink(self):
        # create a symlink to /usr/bin/vim
        os.symlink(os.path.join(self.ppim.pkg_img_path, "usr/bin/vim"),
                   os.path.join(self.ppim.pkg_img_path, "usr/bin/vi"))

        self.ppim.symlink_vi()

        # verify that /usr/bin/vi is a symlink to /usr/bin/vim
        self.assert_(os.path.islink(os.path.join(self.ppim.pkg_img_path,
                                                 "usr/bin/vi")))

        vi_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/bin/vi"))
        vim_statinfo = os.stat(os.path.join(self.ppim.pkg_img_path,
                                           "usr/bin/vim"))

        self.assert_(vi_statinfo.st_ino == vim_statinfo.st_ino)


class TestSaveFiles(unittest.TestCase):
    """ test case for testing the saving of important files in /save
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.ppim = LiveCDPrePkgImgMod("Test PPIM")
        self.pi_filelist = ["usr/share/dbus-1/services/", "etc/gconf/schemas/",
                            "usr/share/gnome/autostart/", "etc/xdg/autostart/",
                            "etc/xdg/autostart/updatemanagernotifier.desktop",
                            "usr/share/dbus-1/services/" + \
                            "gnome-power-manager.service",
                            "usr/share/dbus-1/services/" + \
                            "gnome-power-manager.desktop",
                            "usr/share/gnome/autostart/vp-sysmon.desktop",
                            "etc/gconf/schemas/panel-default-setup.entries",
                            "etc/system"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_save_files(self):
        self.ppim.save_files()

        # verify each file or directory is in /save
        for entry in self.pi_filelist:
            if entry.endswith("/"):
                self.assert_(os.path.isdir(os.path.join(self.ppim.pkg_img_path,
                                                        "save", entry)))
            else:
                self.assert_(os.path.isfile(os.path.join(
                    self.ppim.pkg_img_path, "save", entry)))


class TestConfigureSMF(unittest.TestCase):
    """ test case for testing the configuration of SMF
    """

    def setUp(self):
        # create a dummy filesystem with some files created in the proper
        # location
        InstallEngine()
        self.ppim = PrePkgImgMod("Test PPIM")
        self.pi_filelist = ["/etc/svc/", "/lib/svc/manifest/system/",
                            "/var/svc/manifest/system/", "/lib/", "/usr/lib/",
                            "/usr/sbin/", "/etc/inet/hosts"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

        # create two manifests in the pkg_image area
        testlib.create_smf_xml_file("manifest", "varstub", os.path.join(
            self.ppim.pkg_img_path, "var/svc/manifest/system/var_stub.xml"))
        testlib.create_smf_xml_file("manifest", "libstub", os.path.join(
            self.ppim.pkg_img_path, "lib/svc/manifest/system/lib_stub.xml"))

        # create a profile to apply
        (_none, path) = tempfile.mkstemp(dir="/var/tmp", prefix="smf_profile_")
        testlib.create_smf_xml_file("profile", "profilestub", path)

        # save the profile path in a list
        self.ppim.svc_profiles = [path]

        # copy /usr/sbin/svccfg to the pkg_image path
        shutil.copy2("/usr/sbin/svccfg", os.path.join(self.ppim.pkg_img_path,
                                                      "usr/sbin/svccfg"))
        os.chmod(os.path.join(self.ppim.pkg_img_path, "usr/sbin/svccfg"), 0755)

        # add an entry for localhost to /etc/inet/hosts
        with open(os.path.join(
            self.ppim.pkg_img_path, "etc/inet/hosts"), "a") as fh:
            fh.write("127.0.0.1\tlocalhost")

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        os.remove(self.ppim.svc_profiles[0])
        InstallEngine._instance = None

    def test_configure_smf_default_hostname(self):
        # insert a system/identity:node serivce into the var/svc manifest
        manifest = os.path.join(self.ppim.pkg_img_path,
                                "var/svc/manifest/system/var_stub.xml")
        with open(manifest, "r") as fh:
            data = fh.read().splitlines()
        
        for line in NODENAME.split("\n"):
            data.insert(-2, line)

        with open(manifest, "w+") as fh:
            fh.write("\n".join(data))

        self.ppim.configure_smf()

        # set SVCCFG_REPOSITORY to the pkg_img_path
        os.environ.update({"SVCCFG_REPOSITORY":
            os.path.join(self.ppim.pkg_img_path, "etc/svc/repository.db")})

        # verify the instance's general/enabled is set to true
        cmd = [os.path.join(self.ppim.pkg_img_path, "usr/sbin/svccfg"), "-s",
               "libstub:default", "listprop", "general/enabled"]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        outs, _none = p.communicate()

        # the format of the output will look like:
        # 'general/enabled  boolean  true\n'
        #                            ^^^^^
        self.assert_(outs.split()[2] == "true", outs)

        # verify the first line of /etc/inet/hosts has "solaris" in it
        with open(os.path.join(
            self.ppim.pkg_img_path, "etc/inet/hosts"), "r") as fh:
            line = fh.readline()

        self.assertTrue("solaris" in line)

        del os.environ["SVCCFG_REPOSITORY"]

    def test_configure_smf_custom_hostname(self):
        hostname = "hostnametest"

        # insert a system/identity:node serivce into the var/svc manifest
        manifest = os.path.join(self.ppim.pkg_img_path,
                                "var/svc/manifest/system/var_stub.xml")
        with open(manifest, "r") as fh:
            data = fh.read().splitlines()
        
        for line in NODENAME.split("\n"):
            data.insert(-2, line)

        with open(manifest, "w+") as fh:
            fh.write("\n".join(data).replace("solaris", hostname))

        self.ppim.configure_smf()

        # set SVCCFG_REPOSITORY to the pkg_img_path
        os.environ.update({"SVCCFG_REPOSITORY":
            os.path.join(self.ppim.pkg_img_path, "etc/svc/repository.db")})

        # verify the instance's general/enabled is set to true
        cmd = [os.path.join(self.ppim.pkg_img_path, "usr/sbin/svccfg"), "-s",
               "libstub:default", "listprop", "general/enabled"]
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        outs, _none = p.communicate()

        # the format of the output will look like:
        # 'general/enabled  boolean  true\n'
        #                            ^^^^^
        self.assert_(outs.split()[2] == "true", outs)

        # verify the first line of /etc/inet/hosts has "hostnametest" in it
        with open(os.path.join(
            self.ppim.pkg_img_path, "etc/inet/hosts"), "r") as fh:
            line = fh.readline()

        self.assertTrue(hostname in line)

        del os.environ["SVCCFG_REPOSITORY"]


class TestGetPkgVersion(unittest.TestCase):
    """ test case for testing the get_pkg_version method
    """

    def setUp(self):
        eng = InstallEngine()
        self.ppim = AIPrePkgImgMod("Test PPIM")
        self.ppim.pkg_img_path = "/"
        (fd, path) = tempfile.mkstemp(dir="/var/tmp", prefix="test_pkg_ver_")
        self.outfile = path
        os.chmod(self.outfile, 0777)

        # create a dummy DOC entry for the DC_LABEL
        self.ppim.doc = eng.data_object_cache
        self.ppim.doc.volatile.insert_children(DataObjectDict(DC_LABEL, {}))

    def tearDown(self):
        InstallEngine._instance = None
        if os.path.exists(self.outfile):
            os.remove(self.outfile)

    def test_get_pkg_version(self):
        self.ppim.get_pkg_version("SUNWcs")
        
        # verify a version string is present in the DC_PERS_LABEL DOC object
        d = self.ppim.doc.persistent.get_children(
            name=DC_PERS_LABEL)[0].data_dict
        self.assert_("SUNWcs" in d)


class TestZZGenerateGnomeCaches(unittest.TestCase):
    """ test case for testing the generate gnome caches method
    """

    def setUp(self):
        InstallEngine()
        self.ppim = LiveCDPrePkgImgMod("Test PPIM")
        self.pi_filelist = ["/etc/svc/", "/lib/svc/manifest/system/",
                            "/var/svc/manifest/system/", "/lib/", "/usr/lib/",
                            "/usr/sbin/", "/dev/", "/bin/", "/usr/bin/",
                            "etc/gconf/schemas/"]
        self.ppim.pkg_img_path = testlib.create_filesystem(*self.pi_filelist)

        # copy some binaries into the pkg_image path
        bins = ["/usr/sbin/svccfg", "/usr/bin/bash", "/usr/bin/echo",
                "/usr/bin/fc-cache"]
        for entry in bins:
            shutil.copy2(entry, os.path.join(self.ppim.pkg_img_path,
                                             entry.lstrip("/")))
            os.chmod(os.path.join(self.ppim.pkg_img_path, entry), 0755)

        # bash needs a ton of libraries to work
        libs = ["/lib/libsocket.so.1", "/lib/libresolv.so.2",
                "/lib/libnsl.so.1", "/lib/libgen.so.1", "/lib/libc.so.1",
                "/lib/libcurses.so.1", "/lib/libmd.so.1", "/lib/libmp.so.2",
                "/lib/libm.so.2", "/lib/ld.so.1",
                "/usr/lib/libfontconfig.so.1"]
        for entry in libs:
            shutil.copy2(entry, os.path.join(self.ppim.pkg_img_path,
                                             entry.lstrip("/")))
            os.chmod(os.path.join(self.ppim.pkg_img_path, entry), 0755)

    def tearDown(self):
        shutil.rmtree(self.ppim.pkg_img_path, ignore_errors=True)
        InstallEngine._instance = None

    def test_generate_gnome_caches(self):
        # create a dummy script in /lib/dummy
        dummy = os.path.join(self.ppim.pkg_img_path, "lib/dummy")
        with open(dummy, "w+") as fh:
            fh.write("#!/usr/bin/bash -p\n")
            fh.write("PATH=/bin:/usr/bin\n")
            fh.write("METHOD=$1\n")
            fh.write("case $METHOD in\n")
            fh.write("    'start')\n")
            fh.write("    /usr/bin/echo 'start' > /start\n")
            fh.write("    ;;\n")
            fh.write("    'refresh')\n")
            fh.write("    /usr/bin/echo 'refresh' > /refresh\n")
            fh.write("    ;;\n")
            fh.write("esac\n")
        os.chmod(dummy, 0755)

        # set SVCCFG_REPOSITORY to the pkg_img_path
        os.environ.update({"SVCCFG_REPOSITORY": os.path.join(
            self.ppim.pkg_img_path, "etc/svc/repository.db")})

        # create a new transient manifest
        testlib.create_transient_manifest("test-desktop-cache-test",
            os.path.join(self.ppim.pkg_img_path,
                         "lib/svc/manifest/system/trans.xml"), "/lib/dummy")

        # svccfg import the transient manifest
        cmd = [os.path.join(self.ppim.pkg_img_path, "usr/sbin/svccfg"),
               "import", os.path.join(self.ppim.pkg_img_path,
                                      "lib/svc/manifest/system/trans.xml")]
        subprocess.check_call(cmd)

        # generate the caches
        self.ppim.generate_gnome_caches()

        # need to sleep for just a moment to let fork() finish cleaning up
        time.sleep(0.5)

        # verify that /refresh exists in the pkg_image dir
        self.assert_(os.path.isfile(os.path.join(self.ppim.pkg_img_path,
                                                 "refresh")))
