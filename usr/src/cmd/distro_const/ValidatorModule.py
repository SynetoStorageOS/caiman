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
# Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.

"""Module containing validator methods. """

from osol_install.install_utils import space_parse
from osol_install.distro_const.dc_defs import LOCALE_LIST, DC_LOGGER_NAME
import logging
from os import access, X_OK

# =============================================================================
class ValidatorModule:
    """Module containing validator methods, called by methods in the
    DefValProc.py module on the direction of the defval-manifest.

    Each method in this class takes a TreeAccNode of the node to validate,
    and returns True if the node validates, or False if it doesn't.

    Note that through the node passed in, the method has access to the whole
    tree.  This allows the method to scan the tree for other relevant nodes
    and check their characteristics (in case, for example, the node being
    checked needs to have a certain value based on an attribute of a
    different node).

    """
# =============================================================================

    def __init__(self):
        pass

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_abs_path(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the path starts with a /.  """
        return node.get_value().startswith("/")

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_wildcard(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if disallowed_chars are found in the node's
        value.

        """

        disallowed_chars = "*%?[]{}|><()#$\"\'\\"
        value = node.get_value()
        for dchar in disallowed_chars:
            if (value.find(dchar) != -1):
                return True
        return False

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_zero_thru_nine_val(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node string is a numeric value between
        0..9.

        """

        try:
            value = int(node.get_value())
        except (TypeError, ValueError):
            return False
        return (0 <= value <= 9)

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_locale_available(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns true if the locale specified by the node's value is
        available per the "img_params/locale_list".

        """

        checkthis = node.get_value().strip()
        tree = node.get_tree()
        locale_node = tree.find_node(LOCALE_LIST)[0]
        locales = space_parse(locale_node.get_value())
        for locale in locales:
            if (checkthis == locale.strip()):
                return True
        return False

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_non_neg_float(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node's string represents a non-negative
        floating point number.

        """

        try:
            value = float(node.get_value())
        except ValueError:
            return False
        return (0.0 <= value)

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_hostname_ok(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node's string conforms to a valid hostname.
        Valid hostnames are <= 256 chars, and made up only of alpha-numeric
        characters, dashes and periods.  Two periods in a row are not allowed.

        """

        value = node.get_value()
        if (len(value) > 256):  # We should use MAXHOSTNAMELEN <netdb.h>
            return False
        if (value.find("..") != -1):
            return False
        return (value.lstrip("-.").isalnum())

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_unique(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node's string value is unique among all
        nodes which match the same nodepath.

        """

        count = 0
        value = node.get_value()
        tree = node.get_tree()
        path = node.get_path()
        nodepath_matches = tree.find_node(path)
        for match in nodepath_matches:
            if (match.get_value() == value):
                count += 1
        return (count == 1)

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_executable_file(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node's string value is an executable file.
        This will check to make sure that the file exist and the executable
        bit is on.

        """

        fname = node.get_value()
        rv = access(fname, X_OK)
        if (not rv):
            dc_log = logging.getLogger(DC_LOGGER_NAME)
            dc_log.error("%s either doesn't exist or is "
                         "not an executable file" % fname)
        return (rv)

    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
    @staticmethod
    def is_non_empty_string(node):
    # ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
        """Returns True if the node's string value is not empty.
        Returns False otherwise.
        Empty string is defined as "" or "    "
        This function is useful for ensure that <tag></tag> and
        <tag>    </tag> is not specified.
        If "  " is acceptable, you should NOT use this function

        """

        s_val = node.get_value()
        if ((s_val == "") or (s_val.strip() == "")):
            return False

        return True
