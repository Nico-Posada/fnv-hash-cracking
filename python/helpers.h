#pragma once

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <Python.h>
#include <flint/fmpz.h>

#include "context.h"

/*
    Helper functions for interfacing between the types used for the internal crack ctx
    and the python one.
*/

#define MPZ_STR_BUF_SIZE 0x200

// this is a gross way of converting from PyLong to fmpz num, but idc
inline static bool _pylong_to_fmpz(fmpz_t result, PyObject* num) {
    assert(PyLong_CheckExact(num));
    PyObject* num_str_obj = PyObject_Str(num);
    if (num_str_obj == NULL) {
        return false;
    }

    const char* offset_basis_str = PyUnicode_AsUTF8(num_str_obj);
    if (!offset_basis_str) {
        Py_DECREF(num_str_obj);
        return false;
    }

    fmpz_set_str(result, offset_basis_str, 10);
    Py_DECREF(num_str_obj);
    return true;
}

inline static PyObject* _fmpz_to_pylong(const fmpz* num) {
    char buf[MPZ_STR_BUF_SIZE];
    char* output_buf = (char*)buf;
    if (fmpz_sizeinbase(num, 16) + 2 >= MPZ_STR_BUF_SIZE) {
        output_buf = NULL;
    }

    char* ret_str = fmpz_get_str(output_buf, 16, num);
    if (!ret_str) {
        PyErr_Format(PyExc_MemoryError, "Unable to allocate memory to convert from fmpz to Python int.");
        return NULL;
    }

    PyObject* result = PyLong_FromString(ret_str, NULL, 16);
    if (output_buf == NULL) {
        flint_free(ret_str);
    }

    return result;
}

inline static PyObject* _number_to_pylong(fmpz_t fmpz_val, uint64_t u64_val, bool uses_fmpz) {
    if (uses_fmpz) {
        return _fmpz_to_pylong(fmpz_val);
    } else {
        return PyLong_FromUnsignedLongLong(u64_val);
    }
}

inline static PyObject* _char_buffer_to_pyobj(char_buffer* buf) {
    if (buf->data == NULL || buf->length == 0) {
        // will return the cached b""
        return PyBytes_FromStringAndSize(NULL, 0);
    }

    return PyBytes_FromStringAndSize(buf->data, buf->length);
}

inline static PyObject* _fix_ctx_pylong_arg(PyObject* obj, const char* const argname, uint64_t default_arg) {
    if (obj == NULL || Py_IsNone(obj)) {
        return PyLong_FromUnsignedLongLong(default_arg);
    } else if (!PyLong_CheckExact(obj)) {
        PyErr_Format(
            PyExc_TypeError, "%s must be None or an int. Got object of type '%.200s'", argname, Py_TYPE(obj)->tp_name
        );
        return NULL;
    } else {
        PyObject* zero = PyLong_FromLong(0);
        if (zero == NULL) {
            return NULL;
        }

        int is_negative = PyObject_RichCompareBool(obj, zero, Py_LT);
        Py_DECREF(zero);
        if (is_negative < 0) {
            return NULL;
        }
        if (is_negative) {
            PyErr_Format(PyExc_ValueError, "%s must be non-negative", argname);
            return NULL;
        }

        return Py_NewRef(obj);
    }
}
#define _fix_ctx_pylong_arg(obj, default_arg) _fix_ctx_pylong_arg(obj, #obj, default_arg)

inline static unsigned long long _pylong_num_bits(PyObject* obj) {
    PyObject* bit_length = PyObject_CallMethod(obj, "bit_length", NULL);
    if (bit_length == NULL) {
        return (unsigned long long)-1;
    }

    const unsigned long long result = PyLong_AsUnsignedLongLong(bit_length);
    Py_DECREF(bit_length);
    return result;
}

inline static bool _ensure_odd_pylong_arg(PyObject* obj, const char* const argname) {
    const unsigned long long low_bits = PyLong_AsUnsignedLongLongMask(obj);
    if (PyErr_Occurred()) {
        return false;
    }

    if ((low_bits & 1) == 0) {
        PyErr_Format(PyExc_ValueError, "%s must be odd", argname);
        return false;
    }

    return true;
}

inline static bool _ensure_uint_pylong_arg_fits(PyObject* obj, const char* const argname, uint32_t bits) {
    PyObject* zero = PyLong_FromLong(0);
    if (zero == NULL) {
        return false;
    }

    int is_negative = PyObject_RichCompareBool(obj, zero, Py_LT);
    Py_DECREF(zero);
    if (is_negative < 0) {
        return false;
    }
    if (is_negative) {
        PyErr_Format(PyExc_ValueError, "%s must be non-negative", argname);
        return false;
    }

    const unsigned long long bit_length = _pylong_num_bits(obj);
    if (bit_length == (unsigned long long)-1 && PyErr_Occurred()) {
        return false;
    }
    if (bit_length > bits) {
        PyErr_Format(PyExc_OverflowError, "%s must fit in uint%u", argname, bits);
        return false;
    }

    return true;
}
static int
_parse_uint32_arg(PyObject* obj, const char* const argname, uint32_t* result, bool optional, uint32_t default_val) {
    if (obj == NULL || Py_IsNone(obj)) {
        if (!optional) {
            if (Py_IsNone(obj)) {
                goto bad_arg;
            }

            PyErr_Format(PyExc_TypeError, "Missing argument value for %s", argname);
            return 0;
        }

        *result = default_val;
        return 2;
    }

    if (!PyLong_CheckExact(obj)) {
    bad_arg:
        PyErr_Format(PyExc_TypeError, "%s must be an int, got '%.200s'", argname, Py_TYPE(obj)->tp_name);
        return 0;
    }

    if (!_ensure_uint_pylong_arg_fits(obj, argname, 32)) {
        return 0;
    }

    unsigned long long temp_val = PyLong_AsUnsignedLongLong(obj);
    if (temp_val == (unsigned long long)-1 && PyErr_Occurred()) {
        return 0;
    }

    *result = (uint32_t)temp_val;
    return 1;
}
#define _parse_uint32_arg(obj, result, optional, default) _parse_uint32_arg(obj, &(#result)[1], result, optional, default)

static int
_parse_uint64_arg(PyObject* obj, const char* const argname, uint64_t* result, bool optional, uint64_t default_val) {
    if (obj == NULL || Py_IsNone(obj)) {
        if (!optional) {
            if (Py_IsNone(obj)) {
                goto bad_arg;
            }

            PyErr_Format(PyExc_TypeError, "Missing argument value for %s", argname);
            return 0;
        }

        *result = default_val;
        return 2;
    }

    if (!PyLong_CheckExact(obj)) {
    bad_arg:
        PyErr_Format(PyExc_TypeError, "%s must be an int, got '%.200s'", argname, Py_TYPE(obj)->tp_name);
        return 0;
    }

    if (!_ensure_uint_pylong_arg_fits(obj, argname, 64)) {
        return 0;
    }

    unsigned long long temp_val = PyLong_AsUnsignedLongLong(obj);
    if (temp_val == (unsigned long long)-1 && PyErr_Occurred()) {
        return 0;
    }

    *result = (uint64_t)temp_val;
    return 1;
}
#define _parse_uint64_arg(obj, result, optional, default) _parse_uint64_arg(obj, &(#result)[1], result, optional, default)

static int _parse_bool_arg(PyObject* obj, const char* const argname, bool* result) {
    if (!PyBool_Check(obj)) {
        PyErr_Format(PyExc_TypeError, "%s must be a bool, got '%.200s'", argname, Py_TYPE(obj)->tp_name);
        return 0;
    }

    *result = Py_IsTrue(obj);
    return 1;
}
#define _parse_bool_arg(obj, result) _parse_bool_arg(obj, &(#result)[1], result)
