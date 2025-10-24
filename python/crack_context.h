#pragma once
#include <stddef.h>
#include <Python.h>
#include "crack.h"

typedef struct {
    PyObject_HEAD
    // actual internal struct
    context_t ctx;
} CrackContext;

static PyTypeObject CrackContextType;
static PyObject* CrackException;

int CrackContext_traverse(CrackContext* self, visitproc visit, void* arg);
void CrackContext_dealloc(CrackContext* self);
PyObject* CrackContext_new(PyTypeObject* type, PyObject* args, PyObject* kwds);

PyObject* CrackContext_get_prime(CrackContext *self, PyObject *);
PyObject* CrackContext_get_offset_basis(CrackContext *self, PyObject *);
PyObject* CrackContext_get_prefix(CrackContext *self, PyObject *);
PyObject* CrackContext_get_suffix(CrackContext *self, PyObject *);
PyObject* CrackContext_get_bit_length(CrackContext *self, PyObject *);
PyObject* CrackContext_get_brute_chars(CrackContext *self, PyObject *);
PyObject* CrackContext_get_valid_chars(CrackContext *self, PyObject *);

PyObject* CrackContext_richcompare(PyObject *v, PyObject *w, int op);
PyObject* CrackContext_repr(CrackContext* obj);

// the good stuff
PyObject* CrackContext_crack(CrackContext* self, PyObject *args, PyObject *kwds);


// rest of the python structs are defined in module.c