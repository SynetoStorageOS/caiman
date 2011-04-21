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
'''
Tests of Manifest Input Module overlay() functionality.
'''

import os
import tempfile
import unittest

import solaris_install.manifest_input as milib

from lxml import etree
from solaris_install import Popen
from solaris_install.manifest_input.mim import ManifestInput

# Eventually bring names into convention.  Skip missing docstrings.
# pylint: disable-msg= C0111, C0103


class TestMIMOverlayCommon(unittest.TestCase):
    '''
    Common setup for Overlay testing.
    '''

    BASE_MANIFEST = "/usr/share/auto_install/default.xml"
    SCHEMA = "/usr/share/install/ai.dtd"
    AIM_MANIFEST_FILE = "/tmp/mim_test.xml"

    MAIN_XML_FILE = "/tmp/test_main.xml"
    OVERLAY_XML_FILE = "/tmp/test_overlay.xml"

    # More descriptive arg name to pass to boolean "incremental" argument.
    OVERLAY = True

    def setUp(self):
        '''
        Specify where the manifest will be built.  Start with no data.
        '''
        os.environ["AIM_MANIFEST"] = self.AIM_MANIFEST_FILE
        if os.path.exists(self.AIM_MANIFEST_FILE):
            os.unlink(self.AIM_MANIFEST_FILE)

    # More descriptive arg name to pass to method below "with_target" arg.
    WITH_TARGET = True

    def create_starting_file(self, with_target=False):
        '''
        Create an XML file most tests start with.
        '''
        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            if with_target:
                main_xml.write('    <target/>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')


class TestOverlayA(TestMIMOverlayCommon):
    '''
    Break a manifest apart and piece it together using overlay functionality.

    This class orchestrates a test whereby a proper manifest (BASE_MANIFEST)
    which (is assumed to) contain <sc_embedded_manifest>, <add_drivers> and
    <software> is split into three sections, each containing one section, and
    overlay is used to put the sections back together into a viable manifest
    again.
    '''

    # BASE_MANIFEST as written out by the XML parser.
    FULL_XML = "/tmp/test_full.xml"

    # Names of the XML files which hold one section apiece.
    SC_EMB_MAN_XML = "/tmp/test_sc_embedded_manifest.xml"
    SOFTWARE_XML = "/tmp/test_software.xml"
    ADD_DRIVER_XML = "/tmp/test_add_drivers.xml"

    # Paths to roots of each of the three sections.
    SC_EMB_SUBTREE = "/auto_install/ai_instance/sc_embedded_manifests"
    SOFTWARE_SUBTREE = "/auto_install/ai_instance/software"
    ADD_DRIVER_SUBTREE = "/auto_install/ai_instance/add_drivers"

    # Diff command.
    DIFF = "/usr/bin/diff"

    def prune(self, subtree):
        '''
        Prune the part of the main tree given by the subtree argument.
        '''
        for tag in self.tree.xpath(subtree):
            tag.getparent().remove(tag)

    @staticmethod
    def strip_blank_lines(filename):
        '''
        Get rid of all lines in a file, which have only white space in them.
        '''
        outfile_fd, temp_file_name = tempfile.mkstemp(text=True)
        outfile = os.fdopen(outfile_fd, 'w')
        with open(filename, 'r') as infile:
            for line in infile:
                if not line.strip():
                    continue
                outfile.write(line)
        outfile.close()
        os.rename(temp_file_name, filename)

    def setUp(self):
        TestMIMOverlayCommon.setUp(self)

        # Assume the manifest used has separate sibling sections for
        # add_drivers, software and sc_embedded_manifest, and no others.
        # Create three files, each with one of the sections.

        # Read in base manifest, and write it out, stripping whitespace lines.
        parser = etree.XMLParser(remove_comments=True)
        self.tree = etree.parse(self.BASE_MANIFEST, parser)
        self.tree.write(self.FULL_XML, pretty_print=True)
        TestOverlayA.strip_blank_lines(self.FULL_XML)

        # Generate the three files with subsections.
        self.prune(self.ADD_DRIVER_SUBTREE)
        self.prune(self.SOFTWARE_SUBTREE)
        self.tree.write(self.SC_EMB_MAN_XML, pretty_print=True)

        self.tree = etree.parse(self.BASE_MANIFEST, parser)
        self.prune(self.ADD_DRIVER_SUBTREE)
        self.prune(self.SC_EMB_SUBTREE)
        self.tree.write(self.SOFTWARE_XML, pretty_print=True)

        self.tree = etree.parse(self.BASE_MANIFEST, parser)
        self.prune(self.SC_EMB_SUBTREE)
        self.prune(self.SOFTWARE_SUBTREE)
        self.tree.write(self.ADD_DRIVER_XML, pretty_print=True)

    def test_overlay_1(self):
        '''
        Put original manifest together from pieces, and verify it.
        '''
        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.SC_EMB_MAN_XML, not self.OVERLAY)
        mim.load(self.ADD_DRIVER_XML, self.OVERLAY)
        mim.load(self.SOFTWARE_XML, self.OVERLAY)
        mim.commit()
        TestOverlayA.strip_blank_lines(self.AIM_MANIFEST_FILE)

        # Raises an exception if diff command finds differences from original.
        Popen.check_call([self.DIFF, self.FULL_XML, self.AIM_MANIFEST_FILE])


class TestOverlayBCommon(TestMIMOverlayCommon):

    def do_test(self):
        '''
        Create an overlay file.  Load main, then overlay and test.
        '''
        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance name="secondname">\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)
        (value, path) = mim.get("/auto_install/ai_instance@name")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]@name",
                          "Error: incorrect pathname returned when getting "
                          "\"name\" attribute")
        self.assertEquals(value, "secondname",
                          "Error changing existing attribute "
                          "of non-leaf node.")


class TestOverlay2(TestOverlayBCommon):

    def test_overlay_2(self):
        '''
        Change an attribute of an existing non-leaf element.
        '''
        self.create_starting_file(self.WITH_TARGET)
        self.do_test()


class TestOverlay3(TestOverlayBCommon):

    def test_overlay_3(self):
        '''
        Change an attribute of an existing non-leaf element.
        '''
        self.create_starting_file()
        self.do_test()


class TestOverlay4(TestMIMOverlayCommon):

    def test_overlay_4(self):
        '''
        Leaf overlayed where same-tagged elements not allowed

        ... replaces existing element and any subtree from it.
        '''
        self.create_starting_file(self.WITH_TARGET)

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance auto_reboot="true"/>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        # Verify the old element with name attribute is gone.
        self.assertRaises(milib.MimMatchError, mim.get,
            "/auto_install/ai_instance@name")

        # Verify the new element with auto_reboot attribute is present.
        (value, path) = mim.get("/auto_install/ai_instance@auto_reboot")
        self.assertEquals(value, "true",
            "Error adding new element with new attribute")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]@auto_reboot",
                          "Error: incorrect pathname returned when getting "
                          "\"auto_reboot\" attribute")


class TestOverlay5(TestMIMOverlayCommon):

    def test_overlay_5(self):
        '''
        Overlay same-tagged non-leaf element with new attr where not allowed.

        Same-tagged non-leaf elements are not allowed.
        '''
        self.create_starting_file(self.WITH_TARGET)

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance auto_reboot="true">\n')
            ovrl_xml.write('    <target>\n')
            ovrl_xml.write('    </target>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        # Verify auto_reboot attribute was added to existing element.
        (value, path) = mim.get("/auto_install/ai_instance@name")
        self.assertEquals(value, "firstname",
            "Error finding original attribute of existing node")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]@name",
                          "Error: incorrect pathname returned when getting "
                          "\"auto_reboot\" attribute")

        (value, path) = mim.get("/auto_install/ai_instance@auto_reboot")
        self.assertEquals(value, "true",
            "Error adding new attribute to existing node")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]@auto_reboot",
                          "Error: incorrect pathname returned when getting "
                          "\"auto_reboot\" attribute")


class TestOverlay6(TestMIMOverlayCommon):

    def test_overlay_6(self):
        '''
        Try to overlay a leaf element (id by value) where it does not belong.

        Give element a value to identify it.
        '''
        # Note: giving a bogus attribute is not checked, only a bogus element.

        self.create_starting_file(self.WITH_TARGET)

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <bogus/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        self.assertRaises(milib.MimInvalidError, mim.load,
            self.OVERLAY_XML_FILE, self.OVERLAY)


class TestOverlay7(TestMIMOverlayCommon):

    def test_overlay_7(self):
        '''
        Try to overlay a leaf element (id by attr) where it does not belong.

        Give element an attribute to identify it.
        '''
        self.create_starting_file(self.WITH_TARGET)

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <bogus bogusattr="junk"/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        self.assertRaises(milib.MimInvalidError, mim.load,
            self.OVERLAY_XML_FILE, self.OVERLAY)


class TestOverlay8(TestMIMOverlayCommon):

    def test_overlay_8(self):
        '''
        Add a new non-leaf element where same-tagged elements are allowed.
        '''
        self.create_starting_file(self.WITH_TARGET)

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <target>\n')
            ovrl_xml.write('      <disk disk_name="newdisk"/>\n')
            ovrl_xml.write('    </target>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        (value, path) = mim.get("/auto_install/ai_instance/"
                                "target/disk@disk_name")
        self.assertEquals(value, "newdisk",
                          "Error adding new same-tagged element")

        # Target[2] means a second element was (properly) added.
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]/target[2]"
                          "/disk[1]@disk_name",
                          "Error: incorrect pathname returned when getting "
                          "newly added element.")


class TestOverlay9(TestMIMOverlayCommon):

    def test_overlay_9(self):
        '''
        Add new leaf elem where same-tagged elements are allowed and none exist
        '''
        self.create_starting_file()

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <target/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

        #pylint: disable-msg=W0201
        self.mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        self.mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        self.mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        # Target[1] means a first target element was (properly) added.
        #pylint: disable-msg=W0612
        (value, path) = self.mim.get("/auto_install/ai_instance/target")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]/target[1]",
                          "Error adding leaf element where like-tagged "
                          "elements are allowed.")


class TestOverlay10(TestOverlay9):

    def test_overlay_10(self):
        '''
        Check path to second same-tagged element.
        '''
        self.test_overlay_9()
        self.mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        # Target[2] means a second target element was (properly) added.
        #pylint: disable-msg=W0612
        (value, path) = self.mim.get("/auto_install/ai_instance/target[2]")
        self.assertEquals(path, "/auto_install[1]/ai_instance[1]/target[2]",
                          "Error adding second like-tagged leaf element.")


class TestOverlayInsertionOrderCommon(TestMIMOverlayCommon):
    '''
    Common code for checking order of sibling elements.
    '''
    def check_insertion_order(self):
        '''
        Verify that target, sofware, add_drivers nodes are present and in order
        '''
        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)
        #pylint: disable-msg=W0212
        ai_instance_node = mim._xpath_search("/auto_install[1]/ai_instance[1]")
        found_target = found_software = found_add_drivers = False
        for child in ai_instance_node[0]:
            if child.tag == "target":
                self.assertTrue(not found_software and not found_add_drivers,
                                "Target element not added before software or "
                                "add_drivers elements")
                found_target = True
                continue
            if child.tag == "software":
                self.assertTrue(found_target and not found_add_drivers,
                                "Software element not added between target "
                                "and add_drivers elements")
                found_software = True
                continue
            if child.tag == "add_drivers":
                self.assertTrue(found_target and found_software,
                                "Add_drivers element not added after target "
                                "and software elements")
                return
        self.assertTrue(found_target, "Target element not added")
        self.assertTrue(found_software, "Software element not added")
        self.assertTrue(found_add_drivers, "Add_drivers element not added")


class TestOverlay11(TestOverlayInsertionOrderCommon):
    ''' add element where element belongs between two existing elements.'''

    def setUp(self):
        TestOverlayInsertionOrderCommon.setUp(self)

        # Set up initial file with <target> and <add_drivers> sections.  DTD
        # specifies that <software> goes between <target> and <add_drivers>.
        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            main_xml.write('    <target/>\n')
            main_xml.write('    <add_drivers/>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <software/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

    def test_overlay_11(self):
        '''
        Verify that software section went between target and add_drivers.
        '''
        self.check_insertion_order()


class TestOverlay12(TestOverlayInsertionOrderCommon):
    '''
    Add element where element belongs before existing elements.
    '''

    def setUp(self):
        TestOverlayInsertionOrderCommon.setUp(self)

        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            main_xml.write('    <software/>\n')
            main_xml.write('    <add_drivers/>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')

        # Set up overlay file with <target> that goes before <software> and
        # <add_drivers>
        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <target/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

    def test_overlay_12(self):
        '''
        Verify that target section went before software and add_drivers.
        '''
        self.check_insertion_order()


class TestOverlay13(TestOverlayInsertionOrderCommon):
    '''
    Add element where element belongs after all existing elements.
    '''

    def setUp(self):
        TestOverlayInsertionOrderCommon.setUp(self)

        # Set up initial file with <target> and <add_drivers> sections.  DTD
        # specifies that <add_drivers> goes after <target> and <software>.
        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            main_xml.write('    <target/>\n')
            main_xml.write('    <software/>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <add_drivers/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

    def test_overlay_13(self):
        '''
        Verify that add_drivers section went after target and software.
        '''
        self.check_insertion_order()


class TestOverlay14(TestMIMOverlayCommon):
    '''
    Place element which normally goes after an element that is not present.

    Like above, but <software> node is missing.  Tests that <add_drivers> gets
    added after <target>.
    '''

    def setUp(self):
        TestMIMOverlayCommon.setUp(self)

        # Set up initial file with <target> and <add_drivers> sections.  DTD
        # specifies that <software> goes between <target> and <add_drivers>.
        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            main_xml.write('    <target/>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <add_drivers/>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

    def test_overlay_14(self):
        '''
        Verify that add_drivers goes after target.

        Normally it would go after software, but software is missing and
        software comes after target.
        '''
        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)
        #pylint: disable-msg=W0212
        ai_instance_node = mim._xpath_search("/auto_install[1]/ai_instance[1]")
        found_target = found_add_drivers = False
        for child in ai_instance_node[0]:
            if child.tag == "target":
                self.assertTrue(not found_add_drivers,
                                "Target element not added before software or "
                                "add_drivers elements")
                found_target = True
                continue
            if child.tag == "add_drivers":
                self.assertTrue(found_target,
                                "Add_drivers element not added after target "
                                "and software elements")
                return
        self.assertTrue(found_target, "Target element not added")
        self.assertTrue(found_add_drivers, "Add_drivers element not added")


class TestOverlay15(TestMIMOverlayCommon):
    '''
    Add element where another element with same tag already exists.

    Start with a tree with target, software and source.  Add another tree also
    with target, software and source.  Are all target nodes, software nodes and
    source nodes together?
    '''

    def setUp(self):
        TestMIMOverlayCommon.setUp(self)

        # Set up initial file with <target> and <add_drivers> sections.  DTD
        # specifies that <software> goes between <target> and <add_drivers>.
        with open(self.MAIN_XML_FILE, "w") as main_xml:
            main_xml.write('<auto_install>\n')
            main_xml.write('  <ai_instance name="firstname">\n')
            main_xml.write('    <target>target1</target>\n')
            main_xml.write('    <software>software1</software>\n')
            main_xml.write('    <source>source1</source>\n')
            main_xml.write('  </ai_instance>\n')
            main_xml.write('</auto_install>\n')

        with open(self.OVERLAY_XML_FILE, "w") as ovrl_xml:
            ovrl_xml.write('<auto_install>\n')
            ovrl_xml.write('  <ai_instance>\n')
            ovrl_xml.write('    <target>target2</target>\n')
            ovrl_xml.write('    <software>software2</software>\n')
            ovrl_xml.write('    <source>source2</source>\n')
            ovrl_xml.write('  </ai_instance>\n')
            ovrl_xml.write('</auto_install>\n')

    def test_overlay_15(self):
        '''
        Interleaves two files with same nodes.  Verifies order.
        '''
        values_order = ["target1", "target2", "software1", "software2",
                         "source1", "source2"]

        mim = ManifestInput(self.AIM_MANIFEST_FILE, self.SCHEMA)
        mim.load(self.MAIN_XML_FILE, not self.OVERLAY)
        mim.load(self.OVERLAY_XML_FILE, self.OVERLAY)

        #pylint: disable-msg=W0212
        ai_instance_node = mim._xpath_search("/auto_install[1]/ai_instance[1]")
        values_order_index = 0
        for child in ai_instance_node[0]:
            self.assertEquals(child.text, values_order[values_order_index],
                "Child \"%s\" is out of order.  "
                "Found \"%s\" in its place at index %d.\n" %
                (child.tag, values_order[values_order_index],
                values_order_index))
            values_order_index += 1
        self.assertEquals(values_order_index, len(values_order),
            "Only %d of %d were found.\n" % (values_order_index,
            len(values_order)))

if __name__ == "__main__":
    unittest.main()
