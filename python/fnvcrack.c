#include <stdio.h>

#ifndef PY_SSIZE_T_CLEAN
#define PY_SSIZE_T_CLEAN
#endif
#include <Python.h>
#include <flint/fmpz.h>

#include "context.h"
#include "inverse.h"
#include "brute_gen.h"
#include "fnv.h"
#include "crack.h"

#include "crack_context.h"

#define FNVCRACK_VERSION "2.0"

//////////////////
// CrackContext //
//////////////////

// static PyMemberDef CrackContext_members[] = {
//     // {"bit_length", Py_T_UINT, offsetof(CrackContext, ctx[0].bits), Py_READONLY, "Value used to calculate the modulus (2^bit_length)"},
//     {NULL}  /* Sentinel */
// };

static PyGetSetDef Custom_getsetters[] = {
    {"offset_basis", (getter)CrackContext_get_offset_basis, (setter)NULL, PyDoc_STR("Offset basis"), NULL},
    {"prime", (getter)CrackContext_get_prime, (setter)NULL, PyDoc_STR("Prime"), NULL},
    {"prefix", (getter)CrackContext_get_prefix, (setter)NULL, PyDoc_STR("Prefix"), NULL},
    {"suffix", (getter)CrackContext_get_suffix, (setter)NULL, PyDoc_STR("Suffix"), NULL},
    {"bit_length", (getter)CrackContext_get_bit_length, (setter)NULL, PyDoc_STR("Value used to calculate the modulus (2 ^ bit_length)"), NULL},
    {"valid_chars", (getter)CrackContext_get_valid_chars, (setter)NULL, PyDoc_STR("Characters that should exist in the crack result"), NULL},
    {"brute_chars", (getter)CrackContext_get_brute_chars, (setter)NULL, PyDoc_STR("Characters will be used when doing the partial brute force"), NULL},
    {NULL}  /* Sentinel */
};

static PyMethodDef CrackContext_methods[] = {
    {"crack", (PyCFunction)CrackContext_crack, METH_VARARGS|METH_KEYWORDS, PyDoc_STR("Try to crack a given hash after setting the crack context.")},
    {NULL}  /* Sentinel */
};

PyTypeObject CrackContextType = {
    .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "fnvcrack._fnvcrack.NativeContext",
    .tp_doc = PyDoc_STR("Context used for the hash cracking process."),
    .tp_basicsize = sizeof(CrackContext),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = CrackContext_new,
    .tp_repr = (reprfunc) CrackContext_repr,
    // .tp_init = (initproc) CrackContext_init,
    .tp_dealloc = (destructor)CrackContext_dealloc,
    // .tp_members = CrackContext_members,
    .tp_methods = CrackContext_methods,
    .tp_getset = Custom_getsetters,
    .tp_richcompare = CrackContext_richcompare,
};

/////////////////
// Main Module //
/////////////////

static PyMethodDef module_methods[] = {
    {NULL, NULL, 0, NULL}  // Sentinel
};

static struct PyModuleDef fnvcrack_module = {
    PyModuleDef_HEAD_INIT,
    "fnvcrack._fnvcrack",
    "Native FNV crack bindings",
    -1,
    module_methods
};

PyObject* CrackException;

/////////////////////////
// Initialization func //
/////////////////////////

PyMODINIT_FUNC PyInit__fnvcrack(void) {
    if (PyType_Ready(&CrackContextType) < 0)
        return NULL;

    PyObject* m = PyModule_Create(&fnvcrack_module);
    if (m == NULL)
        return NULL;

    CrackException = PyErr_NewException("fnvcrack.CrackError", NULL, NULL);
    if (CrackException == NULL) {
        Py_DECREF(m);
        return NULL;
    }
    
    Py_INCREF(CrackException);
    if (PyModule_AddObject(m, "CrackError", CrackException) < 0) {
        Py_DECREF(CrackException);
        Py_DECREF(m);
        return NULL;
    }
    
    if (PyModule_AddObjectRef(m, "NativeContext", (PyObject*)&CrackContextType) < 0) {
        Py_DECREF(m);
        return NULL;
    }

    PyObject* version = PyUnicode_FromString(FNVCRACK_VERSION);
    if (version == NULL) {
        Py_DECREF(m);
        return NULL;
    }

    if (PyModule_AddObject(m, "__version__", version) < 0) {
        Py_DECREF(m);
        Py_DECREF(version);
        return NULL;
    }

    return m;
}
