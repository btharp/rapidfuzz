/* SPDX-License-Identifier: MIT */
/* Copyright © 2020 Max Bachmann */
/* Copyright © 2011 Adam Cohen */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <string>
#include "fuzz.hpp"
#include "utils.hpp"
#include "py_utils.hpp"
#include <nonstd/string_view.hpp>

namespace fuzz = rapidfuzz::fuzz;
namespace utils = rapidfuzz::utils;

bool use_preprocessing(PyObject* processor, bool processor_default) {
    return processor ? PyObject_IsTrue(processor) : processor_default;
}

//TODO: make this the default when partial_ratio accepts strings of different char types
template<typename MatchingFunc>
static PyObject* fuzz_call(bool processor_default, PyObject* args, PyObject* keywds) {
    PyObject *py_s1;
    PyObject *py_s2;
    PyObject *processor = NULL;
    double score_cutoff = 0;
    static const char *kwlist[] = {"s1", "s2", "processor", "score_cutoff", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OO|Od", const_cast<char **>(kwlist),
                                     &py_s1, &py_s2, &processor, &score_cutoff)) {
        return NULL;
    }

    if (py_s1 == Py_None || py_s2 == Py_None) {
        return PyFloat_FromDouble(0);
    }

    if (!PyUnicode_Check(py_s1)) {
        PyErr_SetString(PyExc_TypeError, "s1 must be a string or None");
        return NULL;
    }

    if (!PyUnicode_Check(py_s2)) {
        PyErr_SetString(PyExc_TypeError, "s2 must be a string or None");
        return NULL;
    } 

    if (PyUnicode_READY(py_s1) || PyUnicode_READY(py_s2)) {
        return NULL;
    }

    if (PyCallable_Check(processor)) {
        PyObject *proc_s1 = PyObject_CallFunctionObjArgs(processor, py_s2, NULL);
        if (proc_s1 == NULL) {
            return NULL;
        }

        PyObject *proc_s2 = PyObject_CallFunctionObjArgs(processor, py_s2, NULL);
        if (proc_s2 == NULL) {
            Py_DecRef(proc_s1);
            return NULL;
        }

        auto s1_view = decode_python_string(proc_s1);
        auto s2_view = decode_python_string(proc_s2);
    
        double result = mpark::visit([score_cutoff](auto&& val1, auto&& val2) {
            return MatchingFunc::call(val1, val2, score_cutoff);
        }, s1_view, s2_view);

        Py_DecRef(proc_s1);
        Py_DecRef(proc_s2);

        return PyFloat_FromDouble(result);
    }

    auto s1_view = decode_python_string(py_s1);
    auto s2_view = decode_python_string(py_s2);

    double result;
    if (use_preprocessing(processor, processor_default)) {
        result = mpark::visit([score_cutoff](auto&& val1, auto&& val2) {
            return MatchingFunc::call(
                utils::default_process(val1),
                utils::default_process(val2),
                score_cutoff);
        }, s1_view, s2_view);
    } else {
        result = mpark::visit([score_cutoff](auto&& val1, auto&& val2) {
            return MatchingFunc::call(val1, val2, score_cutoff);
        }, s1_view, s2_view);
    }

    return PyFloat_FromDouble(result);
}


template<typename MatchingFunc>
static PyObject* fuzz_call_old(bool processor_default, PyObject* args, PyObject* keywds) {
    PyObject *py_s1;
    PyObject *py_s2;
    PyObject *processor = NULL;
    double score_cutoff = 0;
    static const char *kwlist[] = {"s1", "s2", "processor", "score_cutoff", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, keywds, "OO|Od", const_cast<char **>(kwlist),
                                     &py_s1, &py_s2, &processor, &score_cutoff)) {
        return NULL;
    }

    if (py_s1 == Py_None || py_s2 == Py_None) {
        return PyFloat_FromDouble(0);
    }

    if (!PyUnicode_Check(py_s1)) {
        PyErr_SetString(PyExc_TypeError, "s1 must be a string or None");
        return NULL;
    }

    if (!PyUnicode_Check(py_s2)) {
        PyErr_SetString(PyExc_TypeError, "s2 must be a string or None");
        return NULL;
    } 

    if (PyUnicode_READY(py_s1) || PyUnicode_READY(py_s2)) {
        return NULL;
    }

    if (PyCallable_Check(processor)) {
        PyObject *proc_s1 = PyObject_CallFunctionObjArgs(processor, py_s1, NULL);
        if (proc_s1 == NULL) {
            return NULL;
        }
        Py_ssize_t len_s1 = PyUnicode_GET_LENGTH(proc_s1);
        wchar_t* buffer_s1 = PyUnicode_AsWideCharString(proc_s1, &len_s1);
        Py_DecRef(proc_s1);
        if (buffer_s1 == NULL) {
            return NULL;
        }

        PyObject *proc_s2 = PyObject_CallFunctionObjArgs(processor, py_s2, NULL);
        if (proc_s2 == NULL) {
            PyMem_Free(buffer_s1);
            return NULL;
        }
        Py_ssize_t len_s2 = PyUnicode_GET_LENGTH(proc_s2);
        wchar_t* buffer_s2 = PyUnicode_AsWideCharString(proc_s2, &len_s2);
        Py_DecRef(proc_s2);
        if (buffer_s2 == NULL) {
            PyMem_Free(buffer_s1);
            return NULL;
        }

        auto result = MatchingFunc::call(
            nonstd::wstring_view(buffer_s1, len_s1),
            nonstd::wstring_view(buffer_s2, len_s2),
            score_cutoff);

        PyMem_Free(buffer_s1);
        PyMem_Free(buffer_s2);

        return PyFloat_FromDouble(result);
    }

    Py_ssize_t len_s1 = PyUnicode_GET_LENGTH(py_s1);
    wchar_t* buffer_s1 = PyUnicode_AsWideCharString(py_s1, &len_s1);
    if (buffer_s1 == NULL) {
        return NULL;
    }

    Py_ssize_t len_s2 = PyUnicode_GET_LENGTH(py_s2);
    wchar_t* buffer_s2 = PyUnicode_AsWideCharString(py_s2, &len_s2);
    if (buffer_s2 == NULL) {
        PyMem_Free(buffer_s1);
        return NULL;
    }

    double result;

    if (use_preprocessing(processor, processor_default)) {
        result = MatchingFunc::call(
            utils::default_process(nonstd::wstring_view(buffer_s1, len_s1)),
            utils::default_process(nonstd::wstring_view(buffer_s2, len_s2)),
            score_cutoff);
    } else {
        result = MatchingFunc::call(
            nonstd::wstring_view(buffer_s1, len_s1),
            nonstd::wstring_view(buffer_s2, len_s2),
            score_cutoff);
    }

    PyMem_Free(buffer_s1);
    PyMem_Free(buffer_s2);

    return PyFloat_FromDouble(result);
}


PyDoc_STRVAR(ratio_docstring,
"ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"calculates a simple ratio between two strings\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. None\n"
"        is used by default.\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100\n\n"
"Example:\n"
"    >>> fuzz.ratio(\"this is a test\", \"this is a test!\")\n"
"    96.55171966552734"
);

struct ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::ratio(std::forward<Args>(args)...);
    }
};

static PyObject* ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<ratio_func>(false, args, keywds);
}


PyDoc_STRVAR(partial_ratio_docstring,
"partial_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"calculates the fuzz.ratio of the optimal string alignment\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. None\n"
"        is used by default.\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100\n\n"
"Example:\n"
"    >>> fuzz.partial_ratio(\"this is a test\", \"this is a test!\")\n"
"    100"
);

struct partial_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::partial_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* partial_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call_old<partial_ratio_func>(false, args, keywds);
}


PyDoc_STRVAR(token_sort_ratio_docstring,
"token_sort_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"sorts the words in the strings and calculates the fuzz.ratio between them\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100\n\n"
"Example:\n"
"    >>> fuzz.token_sort_ratio(\"fuzzy wuzzy was a bear\", \"wuzzy fuzzy was a bear\")\n"
"    100.0"
);

struct token_sort_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::token_sort_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* token_sort_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<token_sort_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(partial_token_sort_ratio_docstring,
"partial_token_sort_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"sorts the words in the strings and calculates the fuzz.partial_ratio between them\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct partial_token_sort_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::partial_token_sort_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* partial_token_sort_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call_old<partial_token_sort_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(token_set_ratio_docstring,
"token_set_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Compares the words in the strings based on unique and common words between them using fuzz.ratio\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100\n\n"
"Example:\n"
"    >>> fuzz.token_sort_ratio(\"fuzzy was a bear\", \"fuzzy fuzzy was a bear\")\n"
"    83.8709716796875\n"
"    >>> fuzz.token_set_ratio(\"fuzzy was a bear\", \"fuzzy fuzzy was a bear\")\n"
"    100.0"
);

struct token_set_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::token_set_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* token_set_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<token_set_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(partial_token_set_ratio_docstring,
"partial_token_set_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Compares the words in the strings based on unique and common words between them using fuzz.partial_ratio\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct partial_token_set_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::partial_token_set_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* partial_token_set_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call_old<partial_token_set_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(token_ratio_docstring,
"token_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Helper method that returns the maximum of fuzz.token_set_ratio and fuzz.token_sort_ratio\n"
"    (faster than manually executing the two functions)\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct token_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::token_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* token_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<token_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(partial_token_ratio_docstring,
"partial_token_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Helper method that returns the maximum of fuzz.partial_token_set_ratio and fuzz.partial_token_sort_ratio\n"
"    (faster than manually executing the two functions)\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct partial_token_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::partial_token_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* partial_token_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call_old<partial_token_ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(WRatio_docstring,
"WRatio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Calculates a weighted ratio based on the other ratio algorithms\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct WRatio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::WRatio(std::forward<Args>(args)...);
    }
};


static PyObject* WRatio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call_old<WRatio_func>(true, args, keywds);
}


PyDoc_STRVAR(QRatio_docstring,
"QRatio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"calculates a quick ratio between two strings using fuzz.ratio\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100\n\n"
"Example:\n"
"    >>> fuzz.QRatio(\"this is a test\", \"this is a test!\")\n"
"    96.55171966552734"
);

static PyObject* QRatio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<ratio_func>(true, args, keywds);
}


PyDoc_STRVAR(quick_lev_ratio_docstring,
"quick_lev_ratio($module, s1, s2, processor = False, score_cutoff = 0)\n"
"--\n\n"
"Calculates a quick estimation of fuzz.ratio by counting uncommon letters between the two sentences.\n"
"Guaranteed to be equal or higher than fuzz.ratio.\n"
"(internally used by fuzz.ratio when providing it with a score_cutoff to speed up the matching)\n\n"
"Args:\n"
"    s1 (str): first string to compare\n"
"    s2 (str): second string to compare\n"
"    processor (Union[bool, Callable]): optional callable that reformats the strings. utils.default_process\n"
"        is used by default, which lowercases the strings and trims whitespace\n"
"    score_cutoff (float): Optional argument for a score threshold as a float between 0 and 100.\n"
"        For ratio < score_cutoff 0 is returned instead. Defaults to 0.\n\n"
"Returns:\n"
"    float: ratio between s1 and s2 as a float between 0 and 100"
);

struct quick_lev_ratio_func {
    template <typename... Args>
    static double call(Args&&... args) {
        return fuzz::quick_lev_ratio(std::forward<Args>(args)...);
    }
};

static PyObject* quick_lev_ratio(PyObject* /*self*/, PyObject* args, PyObject* keywds) {
    return fuzz_call<quick_lev_ratio_func>(true, args, keywds);
}


/* The cast of the function is necessary since PyCFunction values
* only take two PyObject* parameters, and these functions take three.
*/
#define PY_METHOD(x) { #x, (PyCFunction)(void(*)(void))x, METH_VARARGS | METH_KEYWORDS, x##_docstring }
static PyMethodDef methods[] = {
    PY_METHOD(ratio),
    PY_METHOD(partial_ratio),
    PY_METHOD(token_sort_ratio),
    PY_METHOD(partial_token_sort_ratio),
    PY_METHOD(token_set_ratio),
    PY_METHOD(partial_token_set_ratio),
    PY_METHOD(token_ratio),
    PY_METHOD(partial_token_ratio),
    PY_METHOD(WRatio),
    PY_METHOD(QRatio),
    PY_METHOD(quick_lev_ratio),
    {NULL, NULL, 0, NULL}   /* sentinel */
};

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "rapidfuzz.fuzz",
    NULL,
    -1,
    methods,
    NULL,  /* m_slots */
    NULL,  /* m_traverse */
    NULL,     /* m_clear */
    NULL   /* m_free */
};

PyMODINIT_FUNC PyInit_fuzz(void) {
    return PyModule_Create(&moduledef);
}
