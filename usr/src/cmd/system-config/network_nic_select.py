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
# Copyright (c) 2010, 2011, Oracle and/or its affiliates. All rights reserved.
#

'''
Support for allowing the user to select a NIC to configure
'''

import logging

from solaris_install.logger import INSTALL_LOGGER_NAME
from solaris_install.sysconfig import _, SCI_HELP
import solaris_install.sysconfig.profile
from solaris_install.sysconfig.profile.network_info import NetworkInfo
from terminalui.base_screen import BaseScreen, SkipException
from terminalui.list_item import ListItem
from terminalui.scroll_window import ScrollWindow
from terminalui.window_area import WindowArea


LOGGER = None


class NICSelect(BaseScreen):
    '''Allow user to choose which ethernet connection to manually configure'''
    
    MAX_NICS = 15
    
    HEADER_TEXT = _("Manual Network Configuration")
    PARAGRAPH = _("Select the one wired network connection to be configured"
                  " during installation")
    
    HELP_DATA = (SCI_HELP + "/%s/network_manual.txt",
                 _("Manual Network Configuration"))
    HELP_FORMAT = "  %s"
    
    LIST_OFFSET = 2
    
    def __init__(self, main_win):
        global LOGGER
        if LOGGER is None:
            LOGGER = logging.getLogger(INSTALL_LOGGER_NAME + ".sysconfig")
        
        super(NICSelect, self).__init__(main_win)
        self.list_area = WindowArea(1, 0, 0, NICSelect.LIST_OFFSET)
        self.ether_nics = NetworkInfo.find_links()
        self.nic = None
    
    def _show(self):
        '''Create a list of NICs to choose from. If more than 15 NICs are
        found, create a scrolling region to put them in
        
        '''
        self.nic = solaris_install.sysconfig.profile.from_engine().nic
        if self.nic.type != NetworkInfo.MANUAL:
            raise SkipException
        if len(self.ether_nics) == 1:
            self.set_nic_in_profile(self.ether_nics[0])
            raise SkipException
        
        try:
            selected_nic_name = self.nic.nic_name
        except AttributeError:
            selected_nic_name = ""
        
        y_loc = 1
        y_loc += self.center_win.add_paragraph(NICSelect.PARAGRAPH, y_loc)
        
        selected_nic = 0
        
        y_loc += 1
        max_nics = min(NICSelect.MAX_NICS, self.center_win.area.lines - y_loc)
        if len(self.ether_nics) > max_nics:
            columns = self.win_size_x - NICSelect.LIST_OFFSET
            win_area = WindowArea(lines=max_nics, columns=columns,
                                  y_loc=y_loc, x_loc=NICSelect.LIST_OFFSET,
                                  scrollable_lines=len(self.ether_nics))
            window = ScrollWindow(win_area, window=self.center_win)
            y_loc = 0
        else:
            window = self.center_win
        
        for nic in self.ether_nics:
            self.list_area.y_loc = y_loc
            self.list_area.columns = len(nic) + 1
            list_item = ListItem(self.list_area, window=window, text=nic,
                                 data_obj=nic)
            if nic == selected_nic_name:
                selected_nic = list_item
            y_loc += 1
        
        self.main_win.do_update()
        self.center_win.activate_object(selected_nic)
    
    def on_change_screen(self):
        '''Save the highlighted NIC as the selected NIC'''
        selected_nic = self.center_win.get_active_object().data_obj
        self.set_nic_in_profile(selected_nic)
    
    def set_nic_in_profile(self, selected_nic):
        '''Set the name of the selected NIC in the profile '''
        LOGGER.info("Selecting %s for manual configuration", selected_nic)
        nic = solaris_install.sysconfig.profile.from_engine().nic
        nic.nic_name = selected_nic