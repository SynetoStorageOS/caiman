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
# Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
#

include ../../Makefile.cmd

all:=          TARGET= all
clean:=        TARGET= clean
clobber:=      TARGET= clobber
install:=      TARGET= install

MANIFEST_FILES=	dc_livecd.xml \
	dc_ai_x86.xml \
	dc_ai_sparc.xml \
	dc_text_x86.xml \
	dc_text_sparc.xml \
	boot_archive_contents_x86.xml \
	boot_archive_contents_sparc.xml

ROOT_MANIFEST_FILES=	$(MANIFEST_FILES:%=$(ROOTDC)/%)

all:

install: .WAIT $(ROOTDC) \
	$(ROOT_MANIFEST_FILES)

clean:

clobber:

include ../../Makefile.targ