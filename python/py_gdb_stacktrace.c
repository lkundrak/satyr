#include "py_gdb_stacktrace.h"
#include "py_gdb_thread.h"
#include "py_gdb_frame.h"
#include "py_gdb_sharedlib.h"
#include "strbuf.h"
#include "gdb/stacktrace.h"
#include "gdb/thread.h"
#include "gdb/sharedlib.h"
#include "location.h"
#include "normalize.h"

#define stacktrace_doc "satyr.GdbStacktrace - class representing a stacktrace\n" \
                      "Usage:\n" \
                      "satyr.GdbStacktrace() - creates an empty stacktrace\n" \
                      "satyr.GdbStacktrace(str) - parses str and fills the stacktrace object"

#define b_dup_doc "Usage: stacktrace.dup()\n" \
                  "Returns: satyr.GdbStacktrace - a new clone of stacktrace\n" \
                  "Clones the stacktrace object. All new structures are independent " \
                  "on the original object."

#define b_find_crash_frame_doc "Usage: stacktrace.find_crash_frame()\n" \
                               "Returns: satyr.Frame - crash frame\n" \
                               "Finds crash frame in the stacktrace. Also sets the " \
                               "stacktrace.crashframe field."

#define b_find_crash_thread_doc "Usage: stacktrace.find_crash_thread()\n" \
                                "Returns: satyr.Thread - crash thread\n" \
                                "Finds crash thread in the stacktrace. Also sets the " \
                                "stacktrace.crashthread field."

#define b_limit_frame_depth_doc "Usage: stacktrace.limit_frame_depth(N)\n" \
                                "N: positive integer - frame depth\n" \
                                "Crops all threads to only contain first N frames."

#define b_quality_simple_doc "Usage: stacktrace.quality_simple()\n" \
                             "Returns: float - 0..1, stacktrace quality\n" \
                             "Computes the quality from stacktrace itself."

#define b_quality_complex_doc "Usage: stacktrace.quality_complex()\n" \
                              "Returns: float - 0..1, stacktrace quality\n" \
                              "Computes the quality from stacktrace, crash thread and " \
                              "frames around the crash."

#define b_get_duplication_hash_doc "Usage: stacktrace.get_duplication_hash()\n" \
                                   "Returns: string - duplication hash\n" \
                                   "Computes the duplication hash used to compare stacktraces."

#define b_find_address_doc "Usage: stacktrace.find_address(address)\n" \
                           "address: long - address to find" \
                           "Returns: satyr.Sharedlib object or None if not found\n" \
                           "Looks whether the given address belongs to a shared library."

#define b_set_libnames_doc "Usage: stacktrace.set_libnames()\n" \
                           "Sets library names according to sharedlibs data."

#define b_normalize_doc "Usage: stacktrace.normalize()\n" \
                        "Normalizes all threads in the stacktrace."

#define b_to_short_text "Usage: stacktrace.to_short_text([max_frames])\n" \
                        "Returns short text representation of the crash thread. If max_frames is\n" \
                        "specified, the result includes only that much topmost frames.\n"

#define b_crashframe_doc (char *)"Readonly. By default the field contains None. After " \
                         "calling the find_crash_frame method, a reference to " \
                         "satyr.Frame object is stored into the field."

#define b_crashthread_doc (char *)"Readonly. By default the field contains None. After " \
                          "calling the find_crash_thread method, a reference to " \
                          "satyr.Thread object is stored into the field."

#define b_threads_doc (char *)"A list containing the satyr.Thread objects " \
                      "representing threads in the stacktrace."

#define b_libs_doc (char *)"A list containing the satyr.Sharedlib objects " \
                   "representing shared libraries loaded at the moment of crash."

static PyMethodDef
gdb_stacktrace_methods[] =
{
    /* methods */
    { "dup",                  sr_py_gdb_stacktrace_dup,                  METH_NOARGS,  b_dup_doc                  },
    { "find_crash_frame",     sr_py_gdb_stacktrace_find_crash_frame,     METH_NOARGS,  b_find_crash_frame_doc     },
    { "find_crash_thread",    sr_py_gdb_stacktrace_find_crash_thread,    METH_NOARGS,  b_find_crash_thread_doc    },
    { "limit_frame_depth",    sr_py_gdb_stacktrace_limit_frame_depth,    METH_VARARGS, b_limit_frame_depth_doc    },
    { "quality_simple",       sr_py_gdb_stacktrace_quality_simple,       METH_NOARGS,  b_quality_simple_doc       },
    { "quality_complex",      sr_py_gdb_stacktrace_quality_complex,      METH_NOARGS,  b_quality_complex_doc      },
    { "get_duplication_hash", sr_py_gdb_stacktrace_get_duplication_hash, METH_NOARGS,  b_get_duplication_hash_doc },
    { "find_address",         sr_py_gdb_stacktrace_find_address,         METH_VARARGS, b_find_address_doc         },
    { "set_libnames",         sr_py_gdb_stacktrace_set_libnames,         METH_NOARGS,  b_set_libnames_doc         },
    { "normalize",            sr_py_gdb_stacktrace_normalize,            METH_NOARGS,  b_normalize_doc            },
    { "to_short_text",        sr_py_gdb_stacktrace_to_short_text,        METH_VARARGS, b_to_short_text            },
    { NULL },
};

static PyMemberDef
gdb_stacktrace_members[] =
{
    { (char *)"threads",     T_OBJECT_EX, offsetof(struct sr_py_gdb_stacktrace, threads),     0,        b_threads_doc     },
    { (char *)"crashframe",  T_OBJECT_EX, offsetof(struct sr_py_gdb_stacktrace, crashframe),  READONLY, b_crashframe_doc  },
    { (char *)"crashthread", T_OBJECT_EX, offsetof(struct sr_py_gdb_stacktrace, crashthread), READONLY, b_crashthread_doc },
    { (char *)"libs",        T_OBJECT_EX, offsetof(struct sr_py_gdb_stacktrace, libs),        0,        b_libs_doc        },
    { NULL },
};

PyTypeObject sr_py_gdb_stacktrace_type = {
    PyObject_HEAD_INIT(NULL)
    0,
    "satyr.GdbStacktrace",           /* tp_name */
    sizeof(struct sr_py_gdb_stacktrace),        /* tp_basicsize */
    0,                              /* tp_itemsize */
    sr_py_gdb_stacktrace_free,       /* tp_dealloc */
    NULL,                           /* tp_print */
    NULL,                           /* tp_getattr */
    NULL,                           /* tp_setattr */
    NULL,                           /* tp_compare */
    NULL,                           /* tp_repr */
    NULL,                           /* tp_as_number */
    NULL,                           /* tp_as_sequence */
    NULL,                           /* tp_as_mapping */
    NULL,                           /* tp_hash */
    NULL,                           /* tp_call */
    sr_py_gdb_stacktrace_str,        /* tp_str */
    NULL,                           /* tp_getattro */
    NULL,                           /* tp_setattro */
    NULL,                           /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,             /* tp_flags */
    stacktrace_doc,                  /* tp_doc */
    NULL,                           /* tp_traverse */
    NULL,                           /* tp_clear */
    NULL,                           /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    NULL,                           /* tp_iter */
    NULL,                           /* tp_iternext */
    gdb_stacktrace_methods,               /* tp_methods */
    gdb_stacktrace_members,               /* tp_members */
    NULL,                           /* tp_getset */
    NULL,                           /* tp_base */
    NULL,                           /* tp_dict */
    NULL,                           /* tp_descr_get */
    NULL,                           /* tp_descr_set */
    0,                              /* tp_dictoffset */
    NULL,                           /* tp_init */
    NULL,                           /* tp_alloc */
    sr_py_gdb_stacktrace_new,        /* tp_new */
    NULL,                           /* tp_free */
    NULL,                           /* tp_is_gc */
    NULL,                           /* tp_bases */
    NULL,                           /* tp_mro */
    NULL,                           /* tp_cache */
    NULL,                           /* tp_subclasses */
    NULL,                           /* tp_weaklist */
};

/* helpers */
int
stacktrace_prepare_linked_list(struct sr_py_gdb_stacktrace *stacktrace)
{
    int i;
    PyObject *item;

    /* thread */
    struct sr_py_gdb_thread *current = NULL, *prev = NULL;
    for (i = 0; i < PyList_Size(stacktrace->threads); ++i)
    {
        item = PyList_GetItem(stacktrace->threads, i);
        if (!item)
            return -1;

        Py_INCREF(item);
        if (!PyObject_TypeCheck(item, &sr_py_gdb_thread_type))
        {
            Py_XDECREF(current);
            Py_XDECREF(prev);
            PyErr_SetString(PyExc_TypeError,
                            "threads must be a list of satyr.Thread objects");
            return -1;
        }

        current = (struct sr_py_gdb_thread*)item;
        if (thread_prepare_linked_list(current) < 0)
            return -1;

        if (i == 0)
            stacktrace->stacktrace->threads = current->thread;
        else
            prev->thread->next = current->thread;

        Py_XDECREF(prev);
        prev = current;
    }

    if (current)
    {
        current->thread->next = NULL;
        Py_XDECREF(current);
    }

    /* sharedlib */
    struct sr_py_gdb_sharedlib *currentlib = NULL, *prevlib = NULL;
    for (i = 0; i < PyList_Size(stacktrace->libs); ++i)
    {
        item = PyList_GetItem(stacktrace->libs, i);
        if (!item)
            return -1;

        Py_INCREF(item);
        if (!PyObject_TypeCheck(item, &sr_py_gdb_sharedlib_type))
        {
            Py_XDECREF(currentlib);
            Py_XDECREF(prevlib);
            PyErr_SetString(PyExc_TypeError,
                            "libs must be a list of satyr.Sharedlib objects");
            return -1;
        }

        currentlib = (struct sr_py_gdb_sharedlib*)item;
        if (i == 0)
            stacktrace->stacktrace->libs = currentlib->sharedlib;
        else
            prevlib->sharedlib->next = currentlib->sharedlib;

        Py_XDECREF(prevlib);
        prevlib = currentlib;
    }

    if (currentlib)
    {
        currentlib->sharedlib->next = NULL;
        Py_XDECREF(currentlib);
    }

    return 0;
}

int
stacktrace_free_thread_python_list(struct sr_py_gdb_stacktrace *stacktrace)
{
    int i;
    PyObject *item;
    for (i = 0; i < PyList_Size(stacktrace->threads); ++i)
    {
        item = PyList_GetItem(stacktrace->threads, i);
        if (!item)
            return -1;
        Py_DECREF(item);
    }
    Py_DECREF(stacktrace->threads);

    return 0;
}

int
stacktrace_free_sharedlib_python_list(struct sr_py_gdb_stacktrace *stacktrace)
{
    int i;
    PyObject *item;

    for (i = 0; i < PyList_Size(stacktrace->libs); ++i)
    {
        item = PyList_GetItem(stacktrace->libs, i);
        if (!item)
            return -1;
        Py_DECREF(item);
    }
    Py_DECREF(stacktrace->libs);

    return 0;
}

PyObject *
thread_linked_list_to_python_list(struct sr_gdb_stacktrace *stacktrace)
{
    PyObject *result = PyList_New(0);
    if (!result)
        return PyErr_NoMemory();

    struct sr_gdb_thread *thread = stacktrace->threads;
    struct sr_py_gdb_thread *item;
    while (thread)
    {
        item = (struct sr_py_gdb_thread*)
            PyObject_New(struct sr_py_gdb_thread, &sr_py_gdb_thread_type);

        item->thread = thread;
        item->frames = frame_linked_list_to_python_list(thread);
        if (!item->frames)
            return NULL;

        if (PyList_Append(result, (PyObject*)item) < 0)
            return NULL;

        thread = thread->next;
    }

    return result;
}

PyObject *
sharedlib_linked_list_to_python_list(struct sr_gdb_stacktrace *stacktrace)
{
    PyObject *result = PyList_New(0);
    if (!result)
        return PyErr_NoMemory();

    struct sr_gdb_sharedlib *sharedlib = stacktrace->libs;
    struct sr_py_gdb_sharedlib *item;
    while (sharedlib)
    {
        item = (struct sr_py_gdb_sharedlib*)
            PyObject_New(struct sr_py_gdb_sharedlib, &sr_py_gdb_sharedlib_type);

        item->sharedlib = sharedlib;
        if (PyList_Append(result, (PyObject *)item) < 0)
            return NULL;

        sharedlib = sharedlib->next;
    }

    return result;
}

int
stacktrace_rebuild_thread_python_list(struct sr_py_gdb_stacktrace *stacktrace)
{
    struct sr_gdb_thread *newlinkedlist = sr_gdb_thread_dup(stacktrace->stacktrace->threads, true);
    if (!newlinkedlist)
        return -1;
    if (stacktrace_free_thread_python_list(stacktrace) < 0)
    {
        struct sr_gdb_thread *next;
        while (newlinkedlist)
        {
            next = newlinkedlist->next;
            sr_gdb_thread_free(newlinkedlist);
            newlinkedlist = next;
        }
        return -1;
    }
    stacktrace->stacktrace->threads = newlinkedlist;
    stacktrace->threads = thread_linked_list_to_python_list(stacktrace->stacktrace);
    return 0;
}

int
stacktrace_rebuild_sharedlib_python_list(struct sr_py_gdb_stacktrace *stacktrace)
{
    struct sr_gdb_sharedlib *newlinkedlist = sr_gdb_sharedlib_dup(stacktrace->stacktrace->libs, true);
    if (!newlinkedlist)
        return -1;
    if (stacktrace_free_sharedlib_python_list(stacktrace) < 0)
    {
        struct sr_gdb_sharedlib *next;
        while (newlinkedlist)
        {
            next = newlinkedlist->next;
            sr_gdb_sharedlib_free(newlinkedlist);
            newlinkedlist = next;
        }
        return -1;
    }
    stacktrace->stacktrace->libs = newlinkedlist;
    stacktrace->libs = sharedlib_linked_list_to_python_list(stacktrace->stacktrace);
    return 0;
}

/* constructor */
PyObject *
sr_py_gdb_stacktrace_new(PyTypeObject *object,
                          PyObject *args,
                          PyObject *kwds)
{
    struct sr_py_gdb_stacktrace *bo = (struct sr_py_gdb_stacktrace*)
        PyObject_New(struct sr_py_gdb_stacktrace,
                     &sr_py_gdb_stacktrace_type);

    if (!bo)
        return PyErr_NoMemory();

    const char *str = NULL;
    if (!PyArg_ParseTuple(args, "|s", &str))
        return NULL;

    bo->crashframe = (struct sr_py_gdb_frame*)Py_None;
    bo->crashthread = (struct sr_py_gdb_thread*)Py_None;
    if (str)
    {
        /* ToDo parse */
        struct sr_location location;
        sr_location_init(&location);
        bo->stacktrace = sr_gdb_stacktrace_parse(&str, &location);
        if (!bo->stacktrace)
        {
            PyErr_SetString(PyExc_ValueError, location.message);
            return NULL;
        }
        bo->threads = thread_linked_list_to_python_list(bo->stacktrace);
        if (!bo->threads)
            return NULL;
        bo->libs = sharedlib_linked_list_to_python_list(bo->stacktrace);
        if (!bo->libs)
            return NULL;
    }
    else
    {
        bo->threads = PyList_New(0);
        bo->stacktrace = sr_gdb_stacktrace_new();
        bo->libs = PyList_New(0);
    }

    return (PyObject *)bo;
}

/* destructor */
void
sr_py_gdb_stacktrace_free(PyObject *object)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)object;
    stacktrace_free_thread_python_list(this);
    stacktrace_free_sharedlib_python_list(this);
    this->stacktrace->threads = NULL;
    this->stacktrace->libs = NULL;
    sr_gdb_stacktrace_free(this->stacktrace);
    PyObject_Del(object);
}

/* str */
PyObject *
sr_py_gdb_stacktrace_str(PyObject *self)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace *)self;
    struct sr_strbuf *buf = sr_strbuf_new();
    sr_strbuf_append_strf(buf, "Stacktrace with %zd threads",
                           (ssize_t)(PyList_Size(this->threads)));
    char *str = sr_strbuf_free_nobuf(buf);
    PyObject *result = Py_BuildValue("s", str);
    free(str);
    return result;
}

/* methods */
PyObject *
sr_py_gdb_stacktrace_dup(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    struct sr_py_gdb_stacktrace *bo = (struct sr_py_gdb_stacktrace*)
        PyObject_New(struct sr_py_gdb_stacktrace,
                     &sr_py_gdb_stacktrace_type);

    if (!bo)
        return PyErr_NoMemory();

    bo->stacktrace = sr_gdb_stacktrace_dup(this->stacktrace);
    if (!bo->stacktrace)
        return NULL;

    bo->threads = thread_linked_list_to_python_list(bo->stacktrace);
    if (!bo->threads)
        return NULL;

    bo->libs = sharedlib_linked_list_to_python_list(bo->stacktrace);
    if (!bo->libs)
        return NULL;

    if (PyObject_TypeCheck(this->crashthread, &sr_py_gdb_thread_type))
    {
        bo->crashthread = (struct sr_py_gdb_thread *)sr_py_gdb_thread_dup((PyObject*)this->crashthread, PyTuple_New(0));
        if (!bo->crashthread)
            return NULL;
    }
    else
        bo->crashthread = (struct sr_py_gdb_thread*)Py_None;

    if (PyObject_TypeCheck(this->crashframe, &sr_py_gdb_frame_type))
    {
        bo->crashframe = (struct sr_py_gdb_frame*)sr_py_gdb_thread_dup((PyObject*)this->crashframe, PyTuple_New(0));
        if (!bo->crashframe)
            return NULL;
    }
    else
        bo->crashframe = (struct sr_py_gdb_frame*)Py_None;

    return (PyObject*)bo;
}

PyObject *
sr_py_gdb_stacktrace_find_crash_frame(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace *)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* destroys linked list - need to rebuild python list */
    struct sr_gdb_frame *frame = sr_gdb_stacktrace_get_crash_frame(this->stacktrace);
    if (!frame)
    {
        PyErr_SetString(PyExc_LookupError, "Crash frame not found");
        return NULL;
    }

    struct sr_py_gdb_frame *result = (struct sr_py_gdb_frame*)
        PyObject_New(struct sr_py_gdb_frame, &sr_py_gdb_frame_type);

    if (!result)
        return PyErr_NoMemory();

    result->frame = frame;
    this->crashframe = result;

    if (stacktrace_rebuild_thread_python_list(this) < 0)
        return NULL;

    return (PyObject *)result;
}

PyObject *
sr_py_gdb_stacktrace_find_crash_thread(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* destroys linked list - need to rebuild python list */
    struct sr_gdb_thread *thread = sr_gdb_stacktrace_find_crash_thread(this->stacktrace);
    if (!thread)
    {
        PyErr_SetString(PyExc_LookupError, "Crash thread not found");
        return NULL;
    }

    struct sr_py_gdb_thread *result = (struct sr_py_gdb_thread*)
        PyObject_New(struct sr_py_gdb_thread, &sr_py_gdb_thread_type);

    if (!result)
        return PyErr_NoMemory();

    result->thread = sr_gdb_thread_dup(thread, false);
    result->frames = frame_linked_list_to_python_list(result->thread);
    this->crashthread = result;

    if (stacktrace_rebuild_thread_python_list(this) < 0)
        return NULL;

    return (PyObject *)result;
}

PyObject *
sr_py_gdb_stacktrace_limit_frame_depth(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    int depth;
    if (!PyArg_ParseTuple(args, "i", &depth))
        return NULL;

    /* destroys linked list - need to rebuild python list */
    sr_gdb_stacktrace_limit_frame_depth(this->stacktrace, depth);
    if (stacktrace_rebuild_thread_python_list(this) < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyObject *
sr_py_gdb_stacktrace_quality_simple(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* does not destroy the linked list */
    float result = sr_gdb_stacktrace_quality_simple(this->stacktrace);
    return Py_BuildValue("f", result);
}

PyObject *
sr_py_gdb_stacktrace_quality_complex(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* does not destroy the linked list */
    float result = sr_gdb_stacktrace_quality_complex(this->stacktrace);
    return Py_BuildValue("f", result);
}

PyObject *
sr_py_gdb_stacktrace_get_duplication_hash(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* does not destroy the linked list */
    char *duphash = sr_gdb_stacktrace_get_duplication_hash(this->stacktrace);
    PyObject *result = Py_BuildValue("s", duphash);
    free(duphash);

    return result;
}

PyObject *
sr_py_gdb_stacktrace_find_address(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    unsigned long long address;
    if (!PyArg_ParseTuple(args, "l", &address))
        return NULL;

    if (address == -1)
        Py_RETURN_NONE;

    int i;
    struct sr_py_gdb_sharedlib *item;
    for (i = 0; i < PyList_Size(this->libs); ++i)
    {
        item = (struct sr_py_gdb_sharedlib*)PyList_GetItem(this->libs, i);
        if (!item)
            return NULL;

        if (item->sharedlib->from <= address &&
            item->sharedlib->to >= address)
        {
            Py_INCREF(item);
            return (PyObject*)item;
        }
    }

    Py_RETURN_NONE;
}

PyObject *
sr_py_gdb_stacktrace_set_libnames(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* does not destroy the linked list */
    sr_gdb_stacktrace_set_libnames(this->stacktrace);
    if (stacktrace_rebuild_thread_python_list(this) < 0)
        return NULL;

    Py_RETURN_NONE;
}

PyObject *
sr_py_gdb_stacktrace_normalize(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    /* destroys the linked list and frees some parts */
    /* need to rebuild python list manually */
    struct sr_gdb_stacktrace *tmp = sr_gdb_stacktrace_dup(this->stacktrace);
    sr_normalize_gdb_stacktrace(tmp);
    if (stacktrace_free_thread_python_list(this) < 0)
    {
        sr_gdb_stacktrace_free(tmp);
        return NULL;
    }

    this->stacktrace->threads = tmp->threads;
    tmp->threads = NULL;
    sr_gdb_stacktrace_free(tmp);

    this->threads = thread_linked_list_to_python_list(this->stacktrace);
    if (!this->threads)
        return NULL;

    Py_RETURN_NONE;
}

PyObject *
sr_py_gdb_stacktrace_to_short_text(PyObject *self, PyObject *args)
{
    struct sr_py_gdb_stacktrace *this = (struct sr_py_gdb_stacktrace*)self;
    if (stacktrace_prepare_linked_list(this) < 0)
        return NULL;

    int max_frames = 0;
    if (!PyArg_ParseTuple(args, "|i", &max_frames))
        return NULL;

    char *text =
        sr_gdb_stacktrace_to_short_text(this->stacktrace, max_frames);
    if (!text)
    {
        PyErr_SetString(PyExc_LookupError, "Crash thread not found");
        return NULL;
    }

    if (stacktrace_rebuild_thread_python_list(this) < 0)
        return NULL;

    PyObject *result = PyString_FromString(text);

    free(text);
    return result;
}
