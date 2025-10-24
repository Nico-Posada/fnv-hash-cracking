#include "crack_context.h"
#include "helpers.h"

void
CrackContext_dealloc(CrackContext* self) {
    destroy_crack_ctx(self->ctx);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

int
CrackContext_traverse(CrackContext* self, visitproc visit, void* arg) {
    return 0;
}

#define GET_BUFFER_OBJ_SAFE(arg, view, err) \
    if (arg == NULL || Py_IsNone(arg)) { \
        view.buf = NULL; \
        view.len = 0; \
    } \
    else if (PyUnicode_Check(arg)) { \
        PyErr_SetString(PyExc_TypeError, #arg " must be a buffer object, not a str."); \
        err; \
    } \
    else if (!PyObject_CheckBuffer(arg)) { \
        PyErr_Format(PyExc_TypeError, \
                        #arg " must be a buffer object, got '%.200s'", Py_TYPE(arg)->tp_name); \
        err; \
    } \
    else if (PyObject_GetBuffer((arg), &(view), PyBUF_SIMPLE) != 0) { \
        err; \
    } \
    else if ((view).ndim > 1) { \
        PyErr_SetString(PyExc_BufferError, \
                            #arg " buffer must be single dimension"); \
        err; \
    }

#define CLEAR_BUFFER_OBJ_SAFE(view) \
    if (view.buf != NULL) { \
        PyBuffer_Release(&view); \
    }

PyObject*
CrackContext_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PyObject* offset_basis = NULL,
            * prime = NULL,
            * bit_length = NULL,
            * prefix = NULL,
            * suffix = NULL,
            * valid_chars = NULL,
            * brute_chars = NULL;

    bool failed = true;
    PyObject* result = NULL;

    static char* kwlist[] = {
        "offset_basis",
        "prime",
        "bit_length",
        "prefix",
        "suffix",
        "valid_chars",
        "brute_chars",
        NULL
    };

    CrackContext* self = (CrackContext*)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|OOOOOOO", kwlist,
                                     &offset_basis, &prime, &bit_length, &prefix,
                                     &suffix, &valid_chars, &brute_chars)) {
        Py_DECREF(self);
        return NULL;
    }
    
    uint32_t bits;
    if (!_parse_uint32_arg(bit_length, &bits, true, 64)) {
        return NULL;
    }

    if (bits == 0) {
        PyErr_SetString(PyExc_ValueError,
                     "bit_length should be a non-zero value.");
        return NULL;
    }

    self->ctx->bits = bits;
    
    // normalize all buffer args
    Py_buffer prefix_view = {NULL};
    Py_buffer suffix_view = {NULL};
    Py_buffer valid_chars_view = {NULL};
    Py_buffer brute_chars_view = {NULL};
    GET_BUFFER_OBJ_SAFE(prefix, prefix_view, goto fail_buffers);
    GET_BUFFER_OBJ_SAFE(suffix, suffix_view, goto fail_buffers);
    GET_BUFFER_OBJ_SAFE(valid_chars, valid_chars_view, goto fail_buffers);
    GET_BUFFER_OBJ_SAFE(brute_chars, brute_chars_view, goto fail_buffers);

    // normalize int args
    PyObject* new_offset_basis = NULL, * new_prime = NULL;
    new_offset_basis = _fix_ctx_pylong_arg(offset_basis, 0xcbf29ce484222325);
    if (new_offset_basis == NULL) goto fail_ints;
    new_prime = _fix_ctx_pylong_arg(prime, 0x00000100000001b3);
    if (new_prime == NULL) goto fail_ints;
    
#define ENSURE_BIT_SIZE(arg) \
    do { \
        PyObject* tmp = PyObject_CallMethod(arg, "bit_length", NULL); \
        if (tmp == NULL) goto fail_ints; \
        int real_bit_len = PyLong_AsInt(tmp); \
        Py_DECREF(tmp); \
        if (real_bit_len == -1) goto fail_ints; \
        if ((uint32_t)real_bit_len > bits) { \
            PyErr_Format(PyExc_TypeError, #arg " must have a max bit length of %u (your bit_length arg). Got %R which has a bit length of %d", \
                     bits, arg, real_bit_len); goto fail_ints; } \
    } while (0)

    // make sure ints actually fit within the bit length provided
    ENSURE_BIT_SIZE(new_offset_basis);
    ENSURE_BIT_SIZE(new_prime);
#undef ENSURE_BIT_SIZE

// these vars were created in the GET_BUFFER_OBJ_SAFE macro
#define BUFFER_ARG(arg) \
    arg##_view.buf, arg##_view.len

    // initialize the crack ctx
    if (bits <= 64) {
        uint64_t u64_offset_basis = PyLong_AsUnsignedLongLong(new_offset_basis);
        if (u64_offset_basis == (uint64_t)-1 && PyErr_Occurred()) {
            goto fail_ints;
        }

        uint64_t u64_prime = PyLong_AsUnsignedLongLong(new_prime);
        if (u64_prime == (uint64_t)-1 && PyErr_Occurred()) {
            goto fail_ints;
        }

        if (!init_crack_ctx_with_len(
            self->ctx,
            u64_offset_basis,
            u64_prime,
            bits,
            BUFFER_ARG(brute_chars),
            BUFFER_ARG(valid_chars),
            BUFFER_ARG(prefix),
            BUFFER_ARG(suffix)
        )) {
            PyErr_SetString(CrackException,
                            "Failed to initialize crack context (will make more descriptive later)");
            goto fail_ints;
        }
    }
    else {
        fmpz_t fmpz_offset_basis, fmpz_prime;
        fmpz_init(fmpz_offset_basis);
        if (!_pylong_to_fmpz(fmpz_offset_basis, new_offset_basis)) {
            fmpz_clear(fmpz_offset_basis);
            goto fail_ints;
        }

        fmpz_init(fmpz_prime);
        if (!_pylong_to_fmpz(fmpz_prime, new_prime)) {
            fmpz_clear(fmpz_offset_basis);
            fmpz_clear(fmpz_prime);
            goto fail_ints;
        }

        bool ret = init_crack_fmpz_ctx_with_len(
            self->ctx,
            fmpz_offset_basis,
            fmpz_prime,
            bits,
            BUFFER_ARG(brute_chars),
            BUFFER_ARG(valid_chars),
            BUFFER_ARG(prefix),
            BUFFER_ARG(suffix)
        );

        fmpz_clear(fmpz_offset_basis); fmpz_clear(fmpz_prime);

        if (!ret) {
            PyErr_SetString(PyExc_RuntimeError,
                            "Failed to initialize crack context (will make more descriptive later)");
            goto fail_ints;
        }
    }
#undef BUFFER_ARG

    // we survived !!!!
    result = (PyObject*)self;
    failed = false;

fail_ints:
    Py_XDECREF(new_offset_basis);
    Py_XDECREF(new_prime);

fail_buffers:
    CLEAR_BUFFER_OBJ_SAFE(prefix_view);
    CLEAR_BUFFER_OBJ_SAFE(prefix_view);
    CLEAR_BUFFER_OBJ_SAFE(prefix_view);
    CLEAR_BUFFER_OBJ_SAFE(prefix_view);

    if (failed) {
        Py_DECREF(self);
    }

    return result;
}

PyObject*
CrackContext_get_prime(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return _number_to_pylong(get_prime_fmpz(self->ctx), get_prime(self->ctx), self->ctx->uses_fmpz);
}

PyObject*
CrackContext_get_offset_basis(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return _number_to_pylong(get_offset_basis_fmpz(self->ctx), get_offset_basis(self->ctx), self->ctx->uses_fmpz);
}

PyObject*
CrackContext_get_prefix(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return _char_buffer_to_pyobj(get_prefix(self->ctx));
}

PyObject*
CrackContext_get_suffix(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return _char_buffer_to_pyobj(get_suffix(self->ctx));
}

PyObject*
CrackContext_get_bit_length(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return PyLong_FromUnsignedLong((unsigned long)self->ctx->bits);
}

PyObject*
CrackContext_get_brute_chars(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    return _char_buffer_to_pyobj(get_brute_chars(self->ctx));
}

PyObject*
CrackContext_get_valid_chars(CrackContext *self, PyObject *Py_UNUSED(ignored)) {
    uint32_t bytes_needed = 0;
    uint8_t result[256];
    
    for (uint32_t i = 0; i < 256; ++i) {
        if (self->ctx->valid_chars[i] == 1) {
            result[bytes_needed++] = i;
        }
    }

    return PyBytes_FromStringAndSize((const char*)result, bytes_needed);
}

inline static bool _check_equal(context_t lhs, context_t rhs) {
    if (
        lhs->uses_fmpz != rhs->uses_fmpz ||
        lhs->bits != rhs->bits ||
        get_prefix(lhs)->length != get_prefix(rhs)->length ||
        get_suffix(lhs)->length != get_suffix(rhs)->length ||
        get_brute_chars(lhs)->length != get_brute_chars(rhs)->length ||
        memcmp(get_prefix(lhs)->data, get_prefix(rhs)->data, get_prefix(lhs)->length) != 0 ||
        memcmp(get_suffix(lhs)->data, get_suffix(rhs)->data, get_suffix(lhs)->length) != 0 ||
        memcmp(get_brute_chars(lhs)->data, get_brute_chars(rhs)->data, get_brute_chars(lhs)->length) != 0 ||
        memcmp(lhs->valid_chars, rhs->valid_chars, 256) != 0
    ) {
        return false;
    }

    if (lhs->uses_fmpz) {
        return (fmpz_equal(get_prime_fmpz(lhs), get_prime_fmpz(rhs)) &&
                fmpz_equal(get_offset_basis_fmpz(lhs), get_offset_basis_fmpz(rhs)));
    }
    else {
        return (get_prime(lhs) == get_prime(rhs) &&
                get_offset_basis(lhs) == get_offset_basis(rhs));
    }
}

PyObject*
CrackContext_richcompare(PyObject* v, PyObject* w, int op) {
    if (!PyObject_TypeCheck(v, &CrackContextType) ||
        !PyObject_TypeCheck(w, &CrackContextType)) {
        Py_RETURN_NOTIMPLEMENTED;
    }

    switch (op) {
        case Py_NE:
        case Py_EQ:
            break;
        default:
            Py_RETURN_NOTIMPLEMENTED;
    }

    bool result = _check_equal(((CrackContext*)v)->ctx, ((CrackContext*)w)->ctx);
    return PyBool_FromLong((long)((op == Py_NE) ^ result));
}

PyObject*
CrackContext_repr(CrackContext* self) {
    PyObject* prime = NULL,
            * offset_basis = NULL,
            * prefix = NULL,
            * suffix = NULL,
            * valid_chars = NULL,
            * brute_chars = NULL;
    
    PyObject* result = NULL;
    prime = CrackContext_get_prime(self, NULL);
    if (!prime) goto finish;
    offset_basis = CrackContext_get_offset_basis(self, NULL);
    if (!offset_basis) goto finish;
    prefix = CrackContext_get_prefix(self, NULL);
    if (!prefix) goto finish;
    suffix = CrackContext_get_suffix(self, NULL);
    if (!suffix) goto finish;
    valid_chars = CrackContext_get_valid_chars(self, NULL);
    if (!valid_chars) goto finish;
    brute_chars = CrackContext_get_brute_chars(self, NULL);
    if (!brute_chars) goto finish;

    result = PyUnicode_FromFormat(
        "%s(prime=%R, offset_basis=%R, bit_length=%u, prefix=%R, "
        "suffix=%R, valid_chars=%R, brute_chars=%R)",
        Py_TYPE(self)->tp_name,
        prime,
        offset_basis,
        self->ctx->bits,
        prefix,
        suffix,
        valid_chars,
        brute_chars
    );

finish:
    Py_XDECREF(prime);
    Py_XDECREF(offset_basis);
    Py_XDECREF(prefix);
    Py_XDECREF(suffix);
    Py_XDECREF(valid_chars);
    Py_XDECREF(brute_chars);

    return result;
}

PyObject*
CrackContext_crack(CrackContext* self, PyObject *args, PyObject *kwds) {
    static char* kwlist[] = {
        "target",
        "max_search_len",
        "max_crack_len",
        NULL
    };

    PyObject* target = NULL,
            * max_search_len = NULL,
            * max_crack_len = NULL;
    
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO|O", kwlist,
                                     &target, &max_search_len, &max_crack_len)) {
        return NULL;
    }

    if (!is_initialized(self->ctx)) {
        // TODO: add custom excpetion types
        PyErr_SetString(CrackException,
                        "CrackContext uninitialized. Make sure to run the constructor before using this.");
        return NULL;
    }

    uint32_t max_len, max_crack;
    if (!_parse_uint32_arg(max_search_len, &max_len, false, 0)) {
        return NULL;
    }

    // default is the highest brute for the given bits that should have a decently rare chance
    // of generating a collision
    if (!_parse_uint32_arg(max_crack_len, &max_crack, true, self->ctx->bits / 8)) {
        return NULL;
    }

    char_buffer output = { NULL, 0 };
    CrackResult crack_result;
    if (self->ctx->uses_fmpz) {
        fmpz_t target_fmpz;
        fmpz_init(target_fmpz);
        if (!_pylong_to_fmpz(target_fmpz, target)) {
            fmpz_clear(target_fmpz);
            return NULL;
        }
        
        crack_result = crack_fmpz(self->ctx, target_fmpz, &output, max_len, max_crack);
        fmpz_clear(target_fmpz);
    }
    else {
        uint64_t target_u64;
        target_u64 = PyLong_AsUnsignedLongLong(target);
        if (target_u64 == (uint64_t)-1 && PyErr_Occurred()) {
            return NULL;
        }

        crack_result = crack_u64(self->ctx, target_u64, &output, max_len, max_crack);
    }

    PyObject* result;
    if ((int)crack_result >= 0) {
        result = Py_BuildValue("(iy#)", crack_result, output.data, output.length);
    }
    else {
        result = Py_BuildValue("(iO)", crack_result, Py_None);
    }

    clear_char_buffer(&output);
    return result;
}