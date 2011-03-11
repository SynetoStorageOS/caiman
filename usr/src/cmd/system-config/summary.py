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
# Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
#

'''
Display a summary of the user's selections
'''

import curses
import logging

from solaris_install.logger import INSTALL_LOGGER_NAME
from solaris_install.engine import InstallEngine
from solaris_install.sysconfig import _, SCI_HELP
import solaris_install.sysconfig.profile
from solaris_install.sysconfig.profile.network_info import NetworkInfo
from solaris_install.sysconfig.profile.user_info import UserInfo

from terminalui.action import Action
from terminalui.base_screen import BaseScreen
from terminalui.i18n import convert_paragraph
from terminalui.window_area import WindowArea
from terminalui.scroll_window import ScrollWindow


LOGGER = None


class SummaryScreen(BaseScreen):
    '''Display a summary of the SC profile that will be applied
    to the system
    '''
    
    HEADER_TEXT = _("System Configuration Summary")
    PARAGRAPH = _("Review the settings below before continuing."
                  " Go back (F3) to make changes.")
    
    HELP_DATA = (SCI_HELP + "/%s/summary.txt",
                 _("System Configuration Summary"))
    
    INDENT = 2
    
    def __init__(self, main_win):
        global LOGGER
        if LOGGER is None:
            LOGGER = logging.getLogger(INSTALL_LOGGER_NAME + ".sysconfig")
        super(SummaryScreen, self).__init__(main_win)
    
    def set_actions(self):
        '''Replace the default F2_Continue with F2_Apply'''
        install_action = Action(curses.KEY_F2, _("Apply"),
                                self.main_win.screen_list.get_next)
        self.main_win.actions[install_action.key] = install_action
    
    def _show(self):
        '''Prepare a text summary from the DOC and display it
        to the user in a ScrollWindow
        
        '''
        y_loc = 1
        y_loc += self.center_win.add_paragraph(SummaryScreen.PARAGRAPH, y_loc)
        
        y_loc += 1
        self.sysconfig = solaris_install.sysconfig.profile.from_engine()
        summary_text = self.build_summary()
        # Wrap the summary text, accounting for the INDENT (used below in
        # the call to add_paragraph)
        max_chars = self.win_size_x - SummaryScreen.INDENT - 1
        summary_text = convert_paragraph(summary_text, max_chars)
        area = WindowArea(x_loc=0, y_loc=y_loc,
                          scrollable_lines=(len(summary_text)+1))
        area.lines = self.win_size_y - y_loc
        area.columns = self.win_size_x
        scroll_region = ScrollWindow(area, window=self.center_win)
        scroll_region.add_paragraph(summary_text, start_x=SummaryScreen.INDENT)
        
        self.center_win.activate_object(scroll_region)
    
    def on_continue(self):
        '''Have the InstallEngine generate the manifest'''
        eng = InstallEngine.get_instance()
        (status, failed_cps) = eng.execute_checkpoints()
        
        if status != InstallEngine.EXEC_SUCCESS:
            print _("Error when generating SC profile\n")
        else:
            print _("SC profile successfully generated\n")
    
    def build_summary(self):
        '''Build a textual summary from solaris_install.sysconfig profile'''
        
        if self.sysconfig is None:
            return ""
        else:
            summary_text = []
        
            summary_text.append(self.get_tz_summary())
            summary_text.append("")
            summary_text.append(_("Language: *The following can be changed "
                                  "when logging in."))
            if self.sysconfig.system.locale is None:
                self.sysconfig.system.determine_locale()
            summary_text.append(_("  Default language: %s") %
                                self.sysconfig.system.actual_lang)
            summary_text.append("")
            summary_text.append(_("Keyboard layout: *The following can be "
                                  "changed when logging in."))
            summary_text.append(_("  Default keyboard layout: %s") %
                                self.sysconfig.system.keyboard)
            summary_text.append("")
            summary_text.append(_("Terminal type: %s") %
                                self.sysconfig.system.terminal_type)
            summary_text.append("")
            summary_text.append(_("Users:"))
            summary_text.extend(self.get_users())
            summary_text.append("")
            summary_text.append(_("Network:"))
            summary_text.extend(self.get_networks())
        
            return "\n".join(summary_text)

    def get_networks(self):
        '''Build a summary of the networks in the install_profile,
        returned as a list of strings
        
        '''
        network_summary = []
        network_summary.append(_("  Computer name: %s") %
                               self.sysconfig.system.hostname)
        nic = self.sysconfig.nic
        
        if nic.type == NetworkInfo.AUTOMATIC:
            network_summary.append(_("  Network Configuration: Automatic"))
        elif nic.type == NetworkInfo.NONE:
            network_summary.append(_("  Network Configuration: None"))
        else:
            network_summary.append(_("  Manual Configuration: %s")
                                   % nic.nic_name)
            network_summary.append(_("    IP Address: %s") % nic.ip_address)
            network_summary.append(_("    Netmask: %s") % nic.netmask)
            if nic.gateway:
                network_summary.append(_("    Router: %s") % nic.gateway)
            if nic.dns_address:
                network_summary.append(_("    DNS: %s") % nic.dns_address)
            if nic.domain:
                network_summary.append(_("    Domain: %s") % nic.domain)
        return network_summary

    def get_users(self):
        '''Build a summary of the user information, and return it as a list
        of strings
        
        '''
        root = self.sysconfig.users[UserInfo.ROOT_IDX]
        primary = self.sysconfig.users[UserInfo.PRIMARY_IDX]
        user_summary = []
        if not root.password:
            user_summary.append(_("  Warning: No root password set"))
        if primary.login_name:
            user_summary.append(_("  Username: %s") % primary.login_name)
        else:
            user_summary.append(_("  No user account"))
        return user_summary
    
    def get_tz_summary(self):
        '''Return a string summary of the timezone selection'''
        timezone = self.sysconfig.system.tz_timezone
        return _("Time Zone: %s") % timezone

