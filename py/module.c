#include <Python.h>
#include "ultraws.h"

/* Example binding */
static PyObject* py_ultraws_connect(PyObject* self, PyObject* args) {
    const char* uri;
    if (!PyArg_ParseTuple(args, "s", &uri)) return NULL;
    ultraws_t* ws = ultraws_connect(uri);
    if (!ws) Py_RETURN_NONE;
    return PyCapsule_New(ws, "ultraws", NULL);
}

/* Module definition */
static PyMethodDef Methods[] = {
    {"connect", py_ultraws_connect, METH_VARARGS, "Connect to a WebSocket"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "ultraws", NULL, -1, Methods
};

PyMODINIT_FUNC PyInit_ultraws(void) { return PyModule_Create(&module); }
