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
# Internet Protocol network interface names and interface indexes 
# software library makefile (if_nametoindex, if_indextoname, if_nameindex)
#

SUBDIRS         = $(MACH)
SUBDIRS         += $(MACH64)

all :=          TARGET= all
clean :=        TARGET= clean
clobber :=      TARGET= clobber
install :=      TARGET= install
lint :=         TARGET= lint

.KEEP_STATE:

all clean clobber install lint: $(SUBDIRS)

PRIVHDRS = netif.h

EXPHDRS =
HDRS		= $(EXPHDRS) $(PRIVHDRS)

include ../Makefile.lib

PYVERSION=python2.6
PYMODULES=	

PYCMODULES=	$(PYMODULES:%.py=%.pyc)
ROOTPYMODULES=  $(PYMODULES:%=$(ROOTPYTHONVENDORINSTALLAI)/%)
ROOTPYCMODULES= $(PYCMODULES:%=$(ROOTPYTHONVENDORINSTALLAI)/%)

install_h:

install:	all .WAIT $(SUBDIRS) .WAIT \
		$(ROOTPYTHONVENDOR) \
		$(ROOTPYTHONVENDORINSTALL) \
		$(ROOTPYTHONVENDORINSTALLCMODS) \
		$(ROOTPYTHONVENDORINSTALLMODS) \
		$(ROOTPYMODULES) \
		$(ROOTPYCMODULES)

python:
	$(PYTHON) -m compileall -l $(@D)

all:	$(HDRS) .WAIT python .WAIT $(SUBDIRS)

lint:

$(SUBDIRS):	FRC
	@cd $@; pwd; $(MAKE) $(TARGET)

FRC:

include ../Makefile.targ
