##############################################################################
# Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

# 	      This file is part of Freight by Joshua Miller

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#      http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions
# and limitations under the License.
##############################################################################

all: parcel

parcel:
	$(MAKE) -C udt/src/
	$(MAKE) -C src/

clean:
	$(MAKE) clean -C src/
	$(MAKE) clean -C udt/

INSTALL_PREFIX=/usr/local

install:
	install parcel $(INSTALL_PREFIX)/bin

.PHONY: install