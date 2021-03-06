/*
    core_unwind.h

    Copyright (C) 2010, 2011, 2012  Red Hat, Inc.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
#ifndef SATYR_CORE_UNWIND_H
#define SATYR_CORE_UNWIND_H

#ifdef __cplusplus
extern "C" {
#endif

struct sr_core_stacktrace;
struct sr_gdb_stacktrace;

struct sr_core_stacktrace *
sr_parse_coredump(const char *coredump_filename,
                  const char *executable_filename,
                  char **error_message);

struct sr_core_stacktrace *
sr_parse_coredump_maps(const char *coredump_filename,
                       const char *executable_filename,
                       const char *maps_filename,
                       char **error_message);

struct sr_core_stacktrace *
sr_core_stacktrace_from_gdb(const char *gdb_output,
                            const char *coredump_filename,
                            const char *executable_filename,
                            char **error_message);

struct sr_core_stacktrace *
sr_core_stacktrace_from_gdb_maps(const char *gdb_output,
                                 const char *coredump_filename,
                                 const char *executable_filename,
                                 const char *maps_filename,
                                 char **error_message);

#ifdef __cplusplus
}
#endif

#endif
