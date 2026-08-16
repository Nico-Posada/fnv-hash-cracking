#pragma once
#include <stddef.h>
#ifndef PY_SSIZE_T_CLEAN
#define PY_SSIZE_T_CLEAN
#endif
#include <Python.h>
#include "crack.h"

typedef struct {
    PyObject_HEAD
        // actual internal struct
        context_t ctx;
} CrackContext;

extern PyTypeObject CrackContextType;
extern PyObject* CrackException;

void CrackContext_dealloc(CrackContext* self);
PyObject* CrackContext_new(PyTypeObject* type, PyObject* args, PyObject* kwds);

PyObject* CrackContext_get_prime(CrackContext* self, PyObject*);
PyObject* CrackContext_get_offset_basis(CrackContext* self, PyObject*);
PyObject* CrackContext_get_prefix(CrackContext* self, PyObject*);
PyObject* CrackContext_get_suffix(CrackContext* self, PyObject*);
PyObject* CrackContext_get_valid_chars(CrackContext* self, PyObject*);

PyObject* CrackContext_richcompare(PyObject* v, PyObject* w, int op);
PyObject* CrackContext_repr(CrackContext* obj);

// the good stuff
PyObject* CrackContext_crack(CrackContext* self, PyObject* args, PyObject* kwds);

// rest of the python structs are defined in module.c
