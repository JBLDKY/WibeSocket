#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <structmember.h>

#include "wibesocket/wibesocket.h"

#define CONN_CAPSULE_NAME "wibesocket.conn"

static void conn_capsule_destructor(PyObject* capsule) {
    void* ptr = PyCapsule_GetPointer(capsule, CONN_CAPSULE_NAME);
    if (ptr) {
        (void)wibesocket_close((wibesocket_conn_t*)ptr);
    }
}

static wibesocket_conn_t* get_conn(PyObject* capsule) {
    return (wibesocket_conn_t*)PyCapsule_GetPointer(capsule, CONN_CAPSULE_NAME);
}

static PyObject* py_connect(PyObject* self, PyObject* args, PyObject* kwargs) {
    const char* uri = NULL;
    static char* kwlist[] = {"uri", "handshake_timeout_ms", "max_frame_size", "user_agent", "origin", "protocol", NULL};
    int handshake_timeout_ms = 5000;
    unsigned long max_frame_size = 1UL << 20;
    const char* user_agent = NULL;
    const char* origin = NULL;
    const char* protocol = NULL;
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|i$kzzz", kwlist,
                                     &uri, &handshake_timeout_ms, &max_frame_size,
                                     &user_agent, &origin, &protocol)) {
        return NULL;
    }
    wibesocket_config_t cfg; memset(&cfg, 0, sizeof(cfg));
    cfg.handshake_timeout_ms = (uint32_t)(handshake_timeout_ms > 0 ? handshake_timeout_ms : 5000);
    cfg.max_frame_size = (uint32_t)max_frame_size;
    cfg.user_agent = user_agent;
    cfg.origin = origin;
    cfg.protocol = protocol;
    wibesocket_conn_t* c = wibesocket_connect(uri, &cfg);
    if (!c) Py_RETURN_NONE;
    return PyCapsule_New((void*)c, CONN_CAPSULE_NAME, conn_capsule_destructor);
}

static PyObject* py_send_text(PyObject* self, PyObject* args) {
    PyObject* capsule; PyObject* obj;
    if (!PyArg_ParseTuple(args, "OO", &capsule, &obj)) return NULL;
    wibesocket_conn_t* c = get_conn(capsule);
    if (!c) Py_RETURN_FALSE;
    if (PyUnicode_Check(obj)) {
        PyObject* utf8 = PyUnicode_AsUTF8String(obj);
        if (!utf8) return NULL;
        char* data = PyBytes_AsString(utf8);
        Py_ssize_t len = PyBytes_Size(utf8);
        wibesocket_error_t e = wibesocket_send_text(c, data, (size_t)len);
        Py_DECREF(utf8);
        if (e != WIBESOCKET_OK) Py_RETURN_FALSE;
        Py_RETURN_TRUE;
    } else if (PyBytes_Check(obj)) {
        char* data = NULL; Py_ssize_t len = 0;
        if (PyBytes_AsStringAndSize(obj, &data, &len) < 0) return NULL;
        wibesocket_error_t e = wibesocket_send_text(c, data, (size_t)len);
        if (e != WIBESOCKET_OK) Py_RETURN_FALSE;
        Py_RETURN_TRUE;
    }
    PyErr_SetString(PyExc_TypeError, "send_text expects str or bytes");
    return NULL;
}

static PyObject* py_send_binary(PyObject* self, PyObject* args) {
    PyObject* capsule; Py_buffer view;
    if (!PyArg_ParseTuple(args, "Oy*", &capsule, &view)) return NULL;
    wibesocket_conn_t* c = get_conn(capsule);
    if (!c) { PyBuffer_Release(&view); Py_RETURN_FALSE; }
    wibesocket_error_t e = wibesocket_send_binary(c, view.buf, (size_t)view.len);
    PyBuffer_Release(&view);
    if (e != WIBESOCKET_OK) Py_RETURN_FALSE;
    Py_RETURN_TRUE;
}

static PyObject* py_recv(PyObject* self, PyObject* args, PyObject* kwargs) {
    PyObject* capsule; int timeout_ms = 1000;
    static char* kwlist[] = {"conn", "timeout_ms", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O|i", kwlist, &capsule, &timeout_ms)) return NULL;
    wibesocket_conn_t* c = get_conn(capsule);
    if (!c) Py_RETURN_NONE;
    wibesocket_message_t msg; memset(&msg, 0, sizeof(msg));
    wibesocket_error_t e = wibesocket_recv(c, &msg, timeout_ms);
    if (e == WIBESOCKET_ERROR_TIMEOUT || e == WIBESOCKET_ERROR_NOT_READY) Py_RETURN_NONE;
    if (e != WIBESOCKET_OK) {
        PyErr_SetString(PyExc_RuntimeError, wibesocket_error_string(e));
        return NULL;
    }
    /* For safety, copy payload into Python bytes for now. Zero-copy to be added by pinning.
       Return tuple: (type, data: bytes, is_final: bool) */
    PyObject* data = PyBytes_FromStringAndSize((const char*)msg.payload, (Py_ssize_t)msg.payload_len);
    if (!data) return NULL;
    return Py_BuildValue("iNi", (int)msg.type, data, (int)msg.is_final);
}

static PyObject* py_close(PyObject* self, PyObject* args) {
    PyObject* capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule)) return NULL;
    wibesocket_conn_t* c = get_conn(capsule);
    if (c) (void)wibesocket_close(c);
    Py_RETURN_NONE;
}

static PyMethodDef Methods[] = {
    {"connect", (PyCFunction)py_connect, METH_VARARGS | METH_KEYWORDS, "Connect to a WebSocket (non-blocking)."},
    {"send_text", py_send_text, METH_VARARGS, "Send a text message (str or bytes)."},
    {"send_binary", py_send_binary, METH_VARARGS, "Send binary data (bytes-like)."},
    {"recv", (PyCFunction)py_recv, METH_VARARGS | METH_KEYWORDS, "Receive a message; returns (type, bytes, is_final) or None on timeout."},
    {"close", py_close, METH_VARARGS, "Close connection."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "wibesocket", NULL, -1, Methods
};

PyMODINIT_FUNC PyInit_wibesocket(void) { return PyModule_Create(&module); }


