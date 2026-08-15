#include "crack_context.h"
#include "helpers.h"
#include "interrupt.h"

typedef struct {
    PyObject* callback;
    PyThreadState* thread_state;
    bool failed;
} python_crack_callback_ctx_t;

static bool _python_crack_candidate(char_buffer candidate, void* userdata) {
    python_crack_callback_ctx_t* ctx = userdata;
    PyEval_RestoreThread(ctx->thread_state);

    PyObject* candidate_obj = PyBytes_FromStringAndSize(candidate.data, candidate.length);
    PyObject* callback_result = NULL;
    int accepted = -1;
    if (candidate_obj) {
        callback_result = PyObject_CallOneArg(ctx->callback, candidate_obj);
        if (callback_result) {
            accepted = PyObject_IsTrue(callback_result);
        }
    }

    Py_XDECREF(callback_result);
    Py_XDECREF(candidate_obj);
    if (accepted < 0) {
        ctx->failed = true;
    }
    ctx->thread_state = PyEval_SaveThread();
    return accepted != 0;
}

void CrackContext_dealloc(CrackContext* self) {
    destroy_crack_ctx(self->ctx);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

int CrackContext_traverse(CrackContext* self, visitproc visit, void* arg) {
    return 0;
}

#define GET_BUFFER_OBJ_SAFE(arg, view, err)                                                                            \
    if (arg == NULL || Py_IsNone(arg)) {                                                                               \
        view.buf = NULL;                                                                                               \
        view.len = 0;                                                                                                  \
    } else if (PyUnicode_Check(arg)) {                                                                                 \
        PyErr_SetString(PyExc_TypeError, #arg " must be a buffer object, not a str.");                                 \
        err;                                                                                                           \
    } else if (!PyObject_CheckBuffer(arg)) {                                                                           \
        PyErr_Format(PyExc_TypeError, #arg " must be a buffer object, got '%.200s'", Py_TYPE(arg)->tp_name);           \
        err;                                                                                                           \
    } else if (PyObject_GetBuffer((arg), &(view), PyBUF_SIMPLE) != 0) {                                                \
        err;                                                                                                           \
    } else if ((view).ndim > 1) {                                                                                      \
        PyErr_SetString(PyExc_BufferError, #arg " buffer must be single dimension");                                   \
        err;                                                                                                           \
    }

#define CLEAR_BUFFER_OBJ_SAFE(view)                                                                                    \
    if (view.buf != NULL) {                                                                                            \
        PyBuffer_Release(&view);                                                                                       \
    }

PyObject* CrackContext_new(PyTypeObject* type, PyObject* args, PyObject* kwds) {
    PyObject *offset_basis = NULL, *prime = NULL, *bit_length = NULL, *prefix = NULL, *suffix = NULL,
             *valid_chars = NULL;

    bool failed = true;
    PyObject* result = NULL;

    static char* kwlist[] = {"offset_basis", "prime", "bit_length", "prefix", "suffix", "valid_chars", NULL};

    CrackContext* self = (CrackContext*)type->tp_alloc(type, 0);
    if (self == NULL) {
        return NULL;
    }

    if (!PyArg_ParseTupleAndKeywords(
            args, kwds, "|OOOOOO", kwlist, &offset_basis, &prime, &bit_length, &prefix, &suffix, &valid_chars
        )) {
        Py_DECREF(self);
        return NULL;
    }

    uint32_t bits;
    if (!_parse_uint32_arg(bit_length, &bits, true, 64)) {
        Py_DECREF(self);
        return NULL;
    }

    if (bits == 0) {
        PyErr_SetString(PyExc_ValueError, "bit_length should be a non-zero value.");
        Py_DECREF(self);
        return NULL;
    }

    self->ctx->bits = bits;

    // normalize all buffer args
    Py_buffer prefix_view = {NULL};
    Py_buffer suffix_view = {NULL};
    Py_buffer valid_chars_view = {NULL};
    GET_BUFFER_OBJ_SAFE(prefix, prefix_view, goto fail_buffers);
    GET_BUFFER_OBJ_SAFE(suffix, suffix_view, goto fail_buffers);
    GET_BUFFER_OBJ_SAFE(valid_chars, valid_chars_view, goto fail_buffers);

    // normalize int args
    PyObject *new_offset_basis = NULL, *new_prime = NULL;
    new_offset_basis = _fix_ctx_pylong_arg(offset_basis, 0xcbf29ce484222325);
    if (new_offset_basis == NULL)
        goto fail_ints;
    new_prime = _fix_ctx_pylong_arg(prime, 0x00000100000001b3);
    if (new_prime == NULL)
        goto fail_ints;

#define ENSURE_BIT_SIZE(arg)                                                                                           \
    do {                                                                                                               \
        PyObject* tmp = PyObject_CallMethod(arg, "bit_length", NULL);                                                  \
        if (tmp == NULL)                                                                                               \
            goto fail_ints;                                                                                            \
        long real_bit_len = PyLong_AsLong(tmp);                                                                        \
        Py_DECREF(tmp);                                                                                                \
        if (real_bit_len == -1)                                                                                        \
            goto fail_ints;                                                                                            \
        if (real_bit_len < 0 || real_bit_len > UINT32_MAX)                                                             \
            goto fail_ints;                                                                                            \
        if ((uint32_t)real_bit_len > bits) {                                                                           \
            PyErr_Format(                                                                                              \
                PyExc_TypeError,                                                                                       \
                #arg " must have a max bit length of %u (your bit_length arg). Got %R which has a bit length of %ld",  \
                bits,                                                                                                  \
                arg,                                                                                                   \
                real_bit_len                                                                                           \
            );                                                                                                         \
            goto fail_ints;                                                                                            \
        }                                                                                                              \
    } while (0)

    // make sure ints actually fit within the bit length provided
    ENSURE_BIT_SIZE(new_offset_basis);
    ENSURE_BIT_SIZE(new_prime);
#undef ENSURE_BIT_SIZE
    if (!_ensure_odd_pylong_arg(new_prime, "prime")) {
        goto fail_ints;
    }

// these vars were created in the GET_BUFFER_OBJ_SAFE macro
#define BUFFER_ARG(arg)                                                                                                \
    (char_buffer) {                                                                                                    \
        (const char*)arg##_view.buf, arg##_view.len                                                                    \
    }

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
                BUFFER_ARG(valid_chars),
                BUFFER_ARG(prefix),
                BUFFER_ARG(suffix)
            )) {
            PyErr_SetString(CrackException, "Failed to initialize crack context (will make more descriptive later)");
            goto fail_ints;
        }
    } else {
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
            BUFFER_ARG(valid_chars),
            BUFFER_ARG(prefix),
            BUFFER_ARG(suffix)
        );

        fmpz_clear(fmpz_offset_basis);
        fmpz_clear(fmpz_prime);

        if (!ret) {
            PyErr_SetString(
                PyExc_RuntimeError, "Failed to initialize crack context (will make more descriptive later)"
            );
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
    CLEAR_BUFFER_OBJ_SAFE(suffix_view);
    CLEAR_BUFFER_OBJ_SAFE(valid_chars_view);

    if (failed) {
        Py_DECREF(self);
    }

    return result;
}

PyObject* CrackContext_get_prime(CrackContext* self, PyObject* Py_UNUSED(ignored)) {
    return _number_to_pylong(get_prime_fmpz(self->ctx), get_prime(self->ctx), self->ctx->uses_fmpz);
}

PyObject* CrackContext_get_offset_basis(CrackContext* self, PyObject* Py_UNUSED(ignored)) {
    return _number_to_pylong(get_offset_basis_fmpz(self->ctx), get_offset_basis(self->ctx), self->ctx->uses_fmpz);
}

PyObject* CrackContext_get_prefix(CrackContext* self, PyObject* Py_UNUSED(ignored)) {
    return _char_buffer_to_pyobj(get_prefix(self->ctx));
}

PyObject* CrackContext_get_suffix(CrackContext* self, PyObject* Py_UNUSED(ignored)) {
    return _char_buffer_to_pyobj(get_suffix(self->ctx));
}

PyObject* CrackContext_get_valid_chars(CrackContext* self, PyObject* Py_UNUSED(ignored)) {
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
    if (lhs->uses_fmpz != rhs->uses_fmpz || lhs->bits != rhs->bits ||
        get_prefix(lhs)->length != get_prefix(rhs)->length || get_suffix(lhs)->length != get_suffix(rhs)->length ||
        memcmp(get_prefix(lhs)->data, get_prefix(rhs)->data, get_prefix(lhs)->length) != 0 ||
        memcmp(get_suffix(lhs)->data, get_suffix(rhs)->data, get_suffix(lhs)->length) != 0 ||
        memcmp(lhs->valid_chars, rhs->valid_chars, 256) != 0) {
        return false;
    }

    if (lhs->uses_fmpz) {
        return (
            fmpz_equal(get_prime_fmpz(lhs), get_prime_fmpz(rhs)) &&
            fmpz_equal(get_offset_basis_fmpz(lhs), get_offset_basis_fmpz(rhs))
        );
    } else {
        return (get_prime(lhs) == get_prime(rhs) && get_offset_basis(lhs) == get_offset_basis(rhs));
    }
}

PyObject* CrackContext_richcompare(PyObject* v, PyObject* w, int op) {
    if (!PyObject_TypeCheck(v, &CrackContextType) || !PyObject_TypeCheck(w, &CrackContextType)) {
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

PyObject* CrackContext_repr(CrackContext* self) {
    PyObject *prime = NULL, *offset_basis = NULL, *prefix = NULL, *suffix = NULL, *valid_chars = NULL;

    PyObject* result = NULL;
    prime = CrackContext_get_prime(self, NULL);
    if (!prime)
        goto finish;
    offset_basis = CrackContext_get_offset_basis(self, NULL);
    if (!offset_basis)
        goto finish;
    prefix = CrackContext_get_prefix(self, NULL);
    if (!prefix)
        goto finish;
    suffix = CrackContext_get_suffix(self, NULL);
    if (!suffix)
        goto finish;
    valid_chars = CrackContext_get_valid_chars(self, NULL);
    if (!valid_chars)
        goto finish;

    result = PyUnicode_FromFormat(
        "CrackContext(prime=%R, offset_basis=%R, bit_length=%u, prefix=%R, "
        "suffix=%R, valid_chars=%R)",
        prime,
        offset_basis,
        self->ctx->bits,
        prefix,
        suffix,
        valid_chars
    );

finish:
    Py_XDECREF(prime);
    Py_XDECREF(offset_basis);
    Py_XDECREF(prefix);
    Py_XDECREF(suffix);
    Py_XDECREF(valid_chars);

    return result;
}

PyObject* CrackContext_crack(CrackContext* self, PyObject* args, PyObject* kwds) {
    static char* kwlist[] = {
        "target", "crack_len", "enum_bound", "max_enum_candidates", "incremental", "callback", NULL
    };

    PyObject *target = NULL, *crack_len_obj = NULL, *enum_bound_obj = NULL, *max_enum_candidates_obj = NULL,
             *incremental_obj = NULL, *callback_obj = Py_None;

    if (!PyArg_ParseTupleAndKeywords(
            args,
            kwds,
            "OOOOO|O",
            kwlist,
            &target,
            &crack_len_obj,
            &enum_bound_obj,
            &max_enum_candidates_obj,
            &incremental_obj,
            &callback_obj
        )) {
        return NULL;
    }

    if (!Py_IsNone(callback_obj) && !PyCallable_Check(callback_obj)) {
        PyErr_SetString(PyExc_TypeError, "callback must be callable or None");
        return NULL;
    }

    if (!is_initialized(self->ctx)) {
        // TODO: add custom excpetion types
        PyErr_SetString(
            CrackException, "CrackContext uninitialized. Make sure to run the constructor before using this."
        );
        return NULL;
    }

    if (!PyLong_CheckExact(target)) {
        PyErr_Format(PyExc_TypeError, "target must be an int, got '%.200s'", Py_TYPE(target)->tp_name);
        return NULL;
    }
    if (!_ensure_uint_pylong_arg_fits(target, "target", self->ctx->bits)) {
        return NULL;
    }

    uint32_t crack_len, enum_bound;
    uint64_t max_enum_candidates;
    bool incremental;
    if (!_parse_uint32_arg(crack_len_obj, &crack_len, false, 0)) {
        return NULL;
    }
    if (!_parse_uint32_arg(enum_bound_obj, &enum_bound, false, CRACK_DEFAULT_ENUM_BOUND)) {
        return NULL;
    }
    if (!_parse_uint64_arg(max_enum_candidates_obj, &max_enum_candidates, false, CRACK_DEFAULT_MAX_ENUM_CANDIDATES)) {
        return NULL;
    }
    if (!_parse_bool_arg(incremental_obj, "incremental", &incremental)) {
        return NULL;
    }
    const size_t known_len = get_prefix(self->ctx)->length + get_suffix(self->ctx)->length;
    if (known_len > UINT32_MAX || (uint64_t)crack_len + (uint64_t)known_len > UINT32_MAX) {
        PyErr_SetString(PyExc_OverflowError, "crack_len plus prefix and suffix lengths must fit in uint32");
        return NULL;
    }

    if (crack_len == 0 && known_len == 0) {
        return Py_BuildValue("(iO)", BAD_SEARCH_LENGTH, Py_None);
    }

    char_buffer output = {NULL, 0};
    CrackResult crack_result = FAILED;
    fmpz_t target_fmpz;
    bool target_fmpz_initialized = false;
    uint64_t target_u64 = 0;

    if (self->ctx->uses_fmpz) {
        fmpz_init(target_fmpz);
        target_fmpz_initialized = true;
        if (!_pylong_to_fmpz(target_fmpz, target)) {
            fmpz_clear(target_fmpz);
            return NULL;
        }
    } else {
        target_u64 = PyLong_AsUnsignedLongLong(target);
        if (target_u64 == (uint64_t)-1 && PyErr_Occurred()) {
            return NULL;
        }
    }

    if (PyErr_CheckSignals() != 0) {
        if (target_fmpz_initialized) {
            fmpz_clear(target_fmpz);
        }
        return NULL;
    }

    python_crack_callback_ctx_t callback_ctx = {
        .callback = Py_IsNone(callback_obj) ? NULL : Py_NewRef(callback_obj),
        .thread_state = NULL,
        .failed = false,
    };

    bool lease_acquired = false;
    bool sync_failed = false;
    if (fnvcrack_install_interrupt_handler() != 0) {
        if (target_fmpz_initialized) {
            fmpz_clear(target_fmpz);
        }
        Py_XDECREF(callback_ctx.callback);
        PyErr_SetString(CrackException, "Failed to install interrupt handler");
        return NULL;
    }
    lease_acquired = true;

    if (fnvcrack_sync_python_interrupt() != 0) {
        sync_failed = true;
        goto cleanup_acquired;
    }

    callback_ctx.thread_state = PyEval_SaveThread();
    if (self->ctx->uses_fmpz) {
        if (incremental) {
            if (callback_ctx.callback) {
                crack_result = crack_fmpz_callback_limits(
                    self->ctx,
                    target_fmpz,
                    &output,
                    crack_len,
                    enum_bound,
                    max_enum_candidates,
                    _python_crack_candidate,
                    &callback_ctx
                );
            } else {
                crack_result =
                    crack_fmpz_limits(self->ctx, target_fmpz, &output, crack_len, enum_bound, max_enum_candidates);
            }
        } else if (callback_ctx.callback) {
            crack_result = crack_fmpz_with_len_callback_limits(
                self->ctx,
                target_fmpz,
                &output,
                crack_len + (uint32_t)known_len,
                enum_bound,
                max_enum_candidates,
                _python_crack_candidate,
                &callback_ctx
            );
        } else {
            crack_result = crack_fmpz_with_len_limits(
                self->ctx, target_fmpz, &output, crack_len + (uint32_t)known_len, enum_bound, max_enum_candidates
            );
        }
    } else if (incremental) {
        if (callback_ctx.callback) {
            crack_result = crack_u64_callback_limits(
                self->ctx,
                target_u64,
                &output,
                crack_len,
                enum_bound,
                max_enum_candidates,
                _python_crack_candidate,
                &callback_ctx
            );
        } else {
            crack_result =
                crack_u64_limits(self->ctx, target_u64, &output, crack_len, enum_bound, max_enum_candidates);
        }
    } else if (callback_ctx.callback) {
        crack_result = crack_u64_with_len_callback_limits(
            self->ctx,
            target_u64,
            &output,
            crack_len + (uint32_t)known_len,
            enum_bound,
            max_enum_candidates,
            _python_crack_candidate,
            &callback_ctx
        );
    } else {
        crack_result = crack_u64_with_len_limits(
            self->ctx, target_u64, &output, crack_len + (uint32_t)known_len, enum_bound, max_enum_candidates
        );
    }
    PyEval_RestoreThread(callback_ctx.thread_state);

cleanup_acquired:
    if (target_fmpz_initialized) {
        fmpz_clear(target_fmpz);
    }

    int release_result = 0;
    if (lease_acquired) {
        release_result = fnvcrack_restore_interrupt_handler();
        lease_acquired = false;
    }

    if (callback_ctx.failed) {
        clear_char_buffer(&output);
        Py_XDECREF(callback_ctx.callback);
        return NULL;
    }
    Py_XDECREF(callback_ctx.callback);
    bool interrupted = crack_result == INTERRUPTED || fnvcrack_interrupted();

    if (sync_failed) {
        clear_char_buffer(&output);
        return NULL;
    }

    if (interrupted) {
        clear_char_buffer(&output);
    }
    if (PyErr_CheckSignals() != 0) {
        if (!interrupted) {
            clear_char_buffer(&output);
        }
        return NULL;
    }
    if (release_result != 0) {
        if (!interrupted) {
            clear_char_buffer(&output);
        }
        PyErr_SetString(CrackException, "Failed to restore interrupt handler");
        return NULL;
    }
    if (interrupted) {
        return Py_BuildValue("(iO)", INTERRUPTED, Py_None);
    }

    PyObject* result;
    if ((int)crack_result >= 0) {
        result = Py_BuildValue("(iy#)", crack_result, output.data, (Py_ssize_t)output.length);
    } else {
        result = Py_BuildValue("(iO)", crack_result, Py_None);
    }

    clear_char_buffer(&output);
    return result;
}
