/*
    core_unwind_elfutils.c

    Copyright (C) 2012  Red Hat, Inc.

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
#include "utils.h"
#include "internal_utils.h"
#include "core/frame.h"
#include "core/thread.h"
#include "core/stacktrace.h"
#include "internal_unwind.h"

#ifdef WITH_LIBDWFL

#include <stdio.h>
#include <string.h>

struct frame_callback_arg
{
    struct sr_core_thread *thread;
    char *error_msg;
};

struct thread_callback_arg
{
    struct sr_core_stacktrace *stacktrace;
    char *error_msg;
};

static int CB_STOP_UNWIND = DWARF_CB_ABORT+1;

static int
frame_callback(Dwfl_Frame *frame, void *data)
{
    struct frame_callback_arg *frame_arg = data;
    char **error_msg = &frame_arg->error_msg;

    Dwarf_Addr pc;
    bool minus_one;
    if (!dwfl_frame_pc(frame, &pc, &minus_one))
    {
        set_error_dwfl("dwfl_frame_pc");
        return DWARF_CB_ABORT;
    }

    Dwfl *dwfl = dwfl_thread_dwfl(dwfl_frame_thread(frame));
    struct sr_core_frame *result = resolve_frame(dwfl, pc, minus_one);

    /* Do not unwind below __libc_start_main. */
    if (0 == sr_strcmp0(result->function_name, "__libc_start_main"))
    {
        sr_core_frame_free(result);
        return CB_STOP_UNWIND;
    }

    frame_arg->thread->frames =
        sr_core_frame_append(frame_arg->thread->frames, result);

    return DWARF_CB_OK;
}

static int
unwind_thread(Dwfl_Thread *thread, void *data)
{
    struct thread_callback_arg *thread_arg = data;
    char **error_msg = &thread_arg->error_msg;

    struct sr_core_thread *result = sr_core_thread_new();
    if (!result)
    {
        set_error("Failed to initialize stacktrace memory");
        return DWARF_CB_ABORT;
    }
    result->id = (int64_t)dwfl_thread_tid(thread);

    struct frame_callback_arg frame_arg =
    {
        .thread = result,
        .error_msg = NULL
    };

    int ret = dwfl_thread_getframes(thread, frame_callback, &frame_arg);
    if (ret != 0 && ret != CB_STOP_UNWIND)
    {
        if (ret == -1)
            set_error_dwfl("dwfl_thread_getframes");
        else if (ret == DWARF_CB_ABORT)
            *error_msg = frame_arg.error_msg;
        else
            *error_msg = sr_strdup("Unknown error in dwfl_thread_getframes");

        goto abort;
    }

    if (!error_msg && !frame_arg.thread->frames)
    {
        set_error("No frames found for thread id %d", (int)result->id);
        goto abort;
    }

    thread_arg->stacktrace->threads =
        sr_core_thread_append(thread_arg->stacktrace->threads, result);
    return DWARF_CB_OK;

abort:
    sr_core_thread_free(result);
    return DWARF_CB_ABORT;
}

struct sr_core_stacktrace *
sr_parse_coredump(const char *core_file,
                  const char *exe_file,
                  char **error_msg)
{
    struct sr_core_stacktrace *stacktrace = NULL;

    /* Initialize error_msg to 'no error'. */
    if (error_msg)
        *error_msg = NULL;

    struct core_handle *ch = open_coredump(core_file, exe_file, error_msg);
    if (*error_msg)
        return NULL;

    stacktrace = sr_core_stacktrace_new();
    if (!stacktrace)
    {
        set_error("Failed to initialize stacktrace memory");
        goto fail_destroy_handle;
    }

    struct thread_callback_arg thread_arg =
    {
        .stacktrace = stacktrace,
        .error_msg = NULL
    };

    int ret = dwfl_getthreads(ch->dwfl, unwind_thread, &thread_arg);
    if (ret != 0)
    {
        if (ret == -1)
            set_error_dwfl("dwfl_getthreads");
        else if (ret == DWARF_CB_ABORT)
            *error_msg = thread_arg.error_msg;
        else
            *error_msg = sr_strdup("Unknown error in dwfl_getthreads");

        goto fail_destroy_trace;
    }

    stacktrace->executable = sr_strdup(exe_file);
    stacktrace->signal = get_signal_number(ch->eh, core_file);
    /* FIXME: is this the best we can do? */
    stacktrace->crash_thread = stacktrace->threads;

fail_destroy_trace:
    if (*error_msg)
    {
        sr_core_stacktrace_free(stacktrace);
        stacktrace = NULL;
    }
fail_destroy_handle:
    core_handle_free(ch);
    return stacktrace;
}

#endif /* WITH_LIBDWFL */
