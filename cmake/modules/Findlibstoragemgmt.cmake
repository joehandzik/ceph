# Copyright (C) 2007-2012 Hypertable, Inc.
#
# This file is part of Hypertable.
#
# Hypertable is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 3
# of the License, or any later version.
#
# Hypertable is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Hypertable. If not, see <http://www.gnu.org/licenses/>
#

# - Find libstoragemgmt
# Find the libstoragemgmt library and includes
#
# LSM_INCLUDE_DIR - where to find libstoragemgmt.h, etc.
# LSM_LIBRARIES - List of libraries when using libstoragemgmt.
# LSM_FOUND - True if libstoragemgmt found.

find_path(LSM_INCLUDE_DIR libstoragemgmt/libstoragemgmt.h)

find_library(LSM_LIBRARIES storagemgmt)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(libstoragemgmt DEFAULT_MSG LSM_LIBRARIES LSM_INCLUDE_DIR)

mark_as_advanced(LSM_LIBRARIES LSM_INCLUDE_DIR)
