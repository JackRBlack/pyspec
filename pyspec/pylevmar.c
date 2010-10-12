/* 
 
	PyLevmar - Python Bindings for Levmar. GPL2.
	Copyright (c) 2006. Alastair Tse <alastair@liquidx.net>
	Copyright (c) 2008. Dustin Lang <dstndstn@gmail.com>

	Modified to run numpy ... 2010 Stuart Wilkins <stuwilkins@mac.com>
 
    $Id$
 
*/

#include "pylevmar.h"

typedef struct _pylm_callback_data {
    PyObject *func;
    PyObject *jacf;
} pylm_callback_data;

void _pylm_callback(PyObject *func, double *p, double *hx, int m, int n, int  jacobian) {
    int i;
	PyObject *args		= NULL;
    PyObject *result	= NULL;
	
	// marshall parameters from c -> python
	// construct numpy arrays from c 
	
	npy_intp dims_m[1] = {m};
	npy_intp dims_n[1] = {n};
	
	for(i=0;i<m;i++){
		fprintf(stderr, "p[%d] = %lf\n", i, p[i]);
	}
    PyObject *estimate = PyArray_SimpleNewFromData(1, dims_m, PyArray_DOUBLE, p);
    PyObject *measurement = PyArray_SimpleNewFromData(1, dims_n , PyArray_DOUBLE, hx);
    
    args = Py_BuildValue("(OO)", estimate, measurement);

    if (!args) {
        goto cleanup;
    }

    // call func
    result = PyObject_CallObject(func, args);
    if (result == NULL) {
        PyErr_Print();
        goto cleanup;
    }
		
	if(!PyArray_Check(result)){
		PyErr_SetString(PyExc_TypeError, "Return value from callback "
						"should be of numpy array type");
		goto cleanup;		
	}	

    // marshall results from python -> c
    
	npy_intp result_size = PyArray_DIM(result, 0);
	//fprintf(stderr, "result_size = %d\n", (int)result_size);
	
	if ((!jacobian && (result_size == n)) ||
        (jacobian &&  (result_size == m*n))) {

        for (i = 0; i < result_size; i++) {
            double *j = PyArray_GETPTR1(result, i);
			hx[i] = *j;
			//fprintf(stderr, "hx[%d] = %lf\n", i, hx[i]);
        }
		
    } else {
        PyErr_SetString(PyExc_TypeError, "Return value from callback "
                        "should be same size as measurement");
    }

 cleanup:
    
    Py_XDECREF(args);
    Py_XDECREF(estimate);
    Py_XDECREF(measurement);
    Py_XDECREF(result);
    return;
}

void _pylm_func_callback(double *p, double *hx, int m, int n, void *data) {
	pylm_callback_data *pydata = (pylm_callback_data *)data;
    if (pydata->func && PyCallable_Check(pydata->func)) {
        _pylm_callback(pydata->func, p, hx, m, n, 0);
    }
}

void _pylm_jacf_callback(double *p, double *hx, int m, int n, void *data) {
	pylm_callback_data *pydata = (pylm_callback_data *)data;
    if (pydata->jacf && PyCallable_Check(pydata->jacf)) {
        _pylm_callback(pydata->jacf, p, hx, m, n, 1);
    }
}

/* ------- start module methods -------- */

#define PyModule_AddDoubleConstant(mod, name, constant) \
  PyModule_AddObject(mod, name, PyFloat_FromDouble(constant));

//
// Initialize python module 
//

void initpylevmar() {
    PyObject *mod = Py_InitModule3("pylevmar", pylm_functions, pylm_doc);
	
	import_array();
    
    PyModule_AddDoubleConstant(mod, "INIT_MU", LM_INIT_MU);
    PyModule_AddDoubleConstant(mod, "STOP_THRESHU", LM_STOP_THRESH);
    PyModule_AddDoubleConstant(mod, "DIFF_DELTA", LM_DIFF_DELTA);

    PyObject *default_opts = Py_BuildValue("(dddd)", 
                                           LM_INIT_MU, // tau
                                           LM_STOP_THRESH, // eps1
                                           LM_STOP_THRESH, // eps2
                                           LM_STOP_THRESH); //eps3
    PyModule_AddObject(mod, "DEFAULT_OPTS", default_opts);
}

static PyObject *
_pylm_dlevmar_generic(PyObject *mod, PyObject *args, PyObject *kwds,
                     char *argstring, char *kwlist[],
                      int jacobian, int bounds) {
    
    
    PyObject *func			= NULL;
	PyObject *jacf			= NULL; 
	PyObject *initial		= NULL,	*initial_npy		= NULL;
	PyObject *measurements	= NULL, *measurements_npy	= NULL;
    PyObject *lower			= NULL, *lower_npy			= NULL;
	PyObject *upper			= NULL, *upper_npy			= NULL;
	
    PyObject *opts			= NULL, *opts_npy			= NULL;
	PyObject *covar			= NULL;
    PyObject *retval		= NULL;
	PyObject *info			= NULL;
	
	pylm_callback_data *pydata = NULL;
	
    double *c_initial		= NULL;
	double *c_measurements	= NULL;
	double *c_opts			= NULL;
    double *c_lower			= NULL;
	double *c_upper			= NULL;
	
    int	   max_iter = 0;
	int    run_iter = 0;
	int    m = 0, n = 0, i = 0;
    double c_info[LM_INFO_SZ];
	int nopts;

	// If finite-difference approximate Jacobians are used, we
	// need 5 optional params; otherwise 4.
	if (jacobian)
		nopts = 4;
	else
		nopts = 5;


    // parse arguments
    if (!bounds) {
        if (jacobian) {
            if (!PyArg_ParseTupleAndKeywords(args, kwds, argstring, kwlist,
                                             &func, &jacf, &initial,
                                             &measurements, &max_iter, 
                                             &opts, &covar))
                return NULL;
        } else {
            if (!PyArg_ParseTupleAndKeywords(args, kwds, argstring, kwlist,
                                             &func, &initial,
                                             &measurements, &max_iter, 
                                             &opts, &covar))
                return NULL;
        }
    } else {
        if (jacobian) {
            if (!PyArg_ParseTupleAndKeywords(args, kwds, argstring, kwlist,
                                             &func, &jacf, &initial,
                                             &measurements, &lower, &upper, &max_iter, 
                                             &opts, &covar))
                return NULL;
        } else {
            if (!PyArg_ParseTupleAndKeywords(args, kwds, argstring, kwlist,
                                             &func, &initial,
                                             &measurements, &lower, &upper, &max_iter,
                                             &opts, &covar))
                return NULL;
        }
    }
     
    // check each var type
   if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "func must be a callable object");
        return NULL;
    }

    if (!PyArray_Check(initial)) {
        PyErr_SetString(PyExc_TypeError, "initial must be a numpy array");
        return NULL;
    }

    if (!PyArray_Check(measurements)) {
        PyErr_SetString(PyExc_TypeError, "measurements must be a numpy array");
        return NULL;
    }

    if (jacobian && !PyCallable_Check(jacf)) {
        PyErr_SetString(PyExc_TypeError, "jacf must be a callable object");
        return NULL;
    }        

    if (lower && !PyArray_Check(lower)) {
        PyErr_SetString(PyExc_TypeError, "lower bounds must be a numpy array");
        return NULL;
    }
    if (upper && !PyArray_Check(upper)) {
        PyErr_SetString(PyExc_TypeError, "upper bounds must be a numpy array");
        return NULL;
    }

    if (opts && !PyArray_Check(opts) && (PyArray_Size(opts) != nopts)) {
		if (nopts == 4)
			PyErr_SetString(PyExc_TypeError,
							"opts must be a numpy vector of length 4.");
		else
			PyErr_SetString(PyExc_TypeError,
							"opts must be a numpy vector of length 5.");
        return NULL;
    }

    // convert python types into C
	
    pydata = PyMem_Malloc(sizeof(pydata));
    pydata->func = func;
    pydata->jacf = jacf;
	
	initial_npy = PyArray_FROMANY(initial, PyArray_DOUBLE, 0, 0, NPY_INOUT_ARRAY);
	//if(!initial_npy)
	//	goto cleanup;
	
	measurements_npy = PyArray_FROMANY(measurements, PyArray_DOUBLE, 0, 0, NPY_INOUT_ARRAY);
	//if(!measurements_npy)
	//	goto cleanum;
	
	m = PyArray_SIZE(initial_npy);
	n = PyArray_SIZE(measurements_npy);
	//c_initial = PyMem_Malloc(sizeof(double) * m);
	//for(i=0;i<m;i++){
	//	c_initial[i] = *(double *)PyArray_GETPTR1(initial_npy, i);
	//}
	//c_measurements = PyMem_Malloc(sizeof(double) * n);
	//for(i=0;i<n;i++){
	//	c_measurements[i] = *(double *)PyArray_GETPTR1(measurements_npy, i);
	//}
    c_initial = (double *)PyArray_DATA(initial_npy);
	c_measurements = (double *)PyArray_DATA(measurements_npy);
	
	
	if (lower){
		lower_npy = PyArray_FROMANY(lower, PyArray_DOUBLE, 0, 0, NPY_INOUT_ARRAY);
		c_lower = PyArray_DATA(lower_npy);
		// check dims
	}
    if (upper){
		upper_npy = PyArray_FROMANY(upper, PyArray_DOUBLE, 0, 0, NPY_INOUT_ARRAY);
        c_upper = PyArray_DATA(upper_npy);
		// check dims
	}

	if (opts) {
		opts_npy = PyArray_FROMANY(opts, PyArray_DOUBLE, 0, 0, NPY_INOUT_ARRAY);
        c_opts = PyArray_DATA(opts_npy);
    }
    
    // call func
    if (!bounds) {
        if (jacobian) {
            run_iter =  dlevmar_der(_pylm_func_callback, _pylm_jacf_callback,
                                    c_initial, c_measurements, m, n,
									max_iter, c_opts, c_info, NULL, NULL, pydata);
        } else {
			//fprintf(stderr, "Running dlevmar_dif\n");
            run_iter =  dlevmar_dif(_pylm_func_callback, c_initial, c_measurements,
                                    m, n, max_iter, c_opts, c_info, NULL, NULL, pydata);
        }
    } else {
        if (jacobian) {
            run_iter =  dlevmar_bc_der(_pylm_func_callback, _pylm_jacf_callback,
                                       c_initial, c_measurements, m, n,
                                       c_lower, c_upper,
                                       max_iter, c_opts, c_info, NULL, NULL, pydata);
        } else {
            run_iter =  dlevmar_bc_dif(_pylm_func_callback, c_initial, c_measurements,
                                       m, n, c_lower, c_upper,
                                       max_iter, c_opts, c_info, NULL, NULL, pydata);
        }
    }

    // convert results back into python
	
    if (run_iter > 0) {
		npy_intp dims[1] = {m};
		retval = PyArray_SimpleNewFromData(1, dims, PyArray_DOUBLE, c_initial);
    } else {
        retval = Py_None;
        Py_INCREF(Py_None);
    }

    // convert additional information into python
    info = Py_BuildValue("{s:d,s:d,s:d,s:d,s:d,s:d,s:d,s:d,s:d}",
                         "initial_e2", c_info[0],
                         "estimate_e2", c_info[1],
                         "estimate_Jt", c_info[2],
                         "estimate_Dp2", c_info[3],
                         "estimate_mu", c_info[4],
                         "iterations", c_info[5],
                         "termination", c_info[6],
                         "function_evaluations", c_info[7],
                         "jacobian_evaluations", c_info[8]);

	//if (c_measurements) {
	//PyMem_Free(c_measurements); c_measurements = NULL;
    //}
    //if (c_initial) {
    //   PyMem_Free(c_initial); c_initial = NULL;
    //}
    //if (pydata) {
    //    PyMem_Free(pydata); pydata = NULL;
   //}

    return Py_BuildValue("(OiO)", retval, run_iter, info, NULL);
}

static PyObject *
pylm_dlevmar_der(PyObject *mod, PyObject *args, PyObject *kwds)
{
    char *argstring = "OOOOi|OO";
    char *kwlist[] = {"func", "jacf", "initial", "measurements",
                      "max_iter", "opts", "covar", 
                      NULL};
    return _pylm_dlevmar_generic(mod, args, kwds, argstring, kwlist, 1, 0);
}

static PyObject *
pylm_dlevmar_dif(PyObject *mod, PyObject *args, PyObject *kwds)
{
    char *argstring = "OOOi|OO";
    char *kwlist[] = {"func", "initial", "measurements", "max_iter",
                      "opts", "covar", NULL};
    return _pylm_dlevmar_generic(mod, args, kwds, argstring, kwlist, 0, 0);
}

static PyObject *
pylm_dlevmar_bc_der(PyObject *mod, PyObject *args, PyObject *kwds)
{
    char *argstring = "OOOOOOi|OO";
    char *kwlist[] = {"func", "jacf", "initial", "measurements",
                      "lower", "upper",
                      "max_iter", "opts", "covar", 
                      NULL};
    return _pylm_dlevmar_generic(mod, args, kwds, argstring, kwlist, 1, 1);
}

static PyObject *
pylm_dlevmar_bc_dif(PyObject *mod, PyObject *args, PyObject *kwds)
{
    char *argstring = "OOOOOi|OO";
    char *kwlist[] = {"func", "initial", "measurements",
                      "lower", "upper",
                      "max_iter", "opts", "covar", NULL};
    return _pylm_dlevmar_generic(mod, args, kwds, argstring, kwlist, 0, 1);
}

static PyObject *
pylm_dlevmar_chkjac(PyObject *mod, PyObject *args, PyObject *kwds)
{
    PyObject *func		= NULL;
	PyObject *jacf		= NULL;
	PyObject *initial	= NULL;
    PyObject *retval	= NULL;

    pylm_callback_data *pydata = NULL;
	
    double *c_initial	= NULL;
	double *err			= NULL;
	
    int i = 0, m = 0, n = 0;


    static char *kwlist[] = {"func", "jacf", "initial", "n", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "OOOi", kwlist,
                                     &func, &jacf, &initial, &n))
        return NULL;
                                     
    if (!PyCallable_Check(func)) {
        PyErr_SetString(PyExc_TypeError, "func must be a callable object");
        return NULL;
    }

    if (!PyCallable_Check(jacf)) {
        PyErr_SetString(PyExc_TypeError, "jacf must be a callable object");
        return NULL;
    }

    if (!PySequence_Check(initial)) {
        PyErr_SetString(PyExc_TypeError, "initial must be a sequence type");
        return NULL;
    }

    // convert python types into C
    m = PySequence_Size(initial);
    pydata = PyMem_Malloc(sizeof(pydata));
    c_initial = PyMem_Malloc(sizeof(double) * m);
    err = PyMem_Malloc(sizeof(double) * n);

    if (!pydata || !c_initial) {
        PyErr_SetString(PyExc_MemoryError, "Unable to allocate memory");
        return NULL;
    }

    pydata->func = func;
    pydata->jacf = jacf;

    for (i = 0; i < m; i++) {
        PyObject *r = PySequence_GetItem(initial, i);
        c_initial[i] = PyFloat_AsDouble(r);
        Py_XDECREF(r);
    }

    // call func
    dlevmar_chkjac(_pylm_func_callback, _pylm_jacf_callback, 
                   c_initial, m, n, pydata, err);

    // convert results back into python
    retval = PyTuple_New(n);
    for (i = 0; i < n; i++) {
        PyTuple_SetItem(retval, i, PyFloat_FromDouble(err[i]));
    }

    if (c_initial) {
        PyMem_Free(c_initial); c_initial = NULL;
    }

    if (pydata) {
        PyMem_Free(pydata); pydata = NULL;
    }

    return retval;
}
