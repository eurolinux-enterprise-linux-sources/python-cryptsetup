#include <Python.h>
#include "structmember.h"

#include <stdio.h>
#include <string.h>

#include "libcryptsetup.h"

typedef struct {
  PyObject_HEAD

  /* Type-specific fields go here. */
  PyObject *yesDialogCB;
  PyObject *cmdLineLogCB;
  struct interface_callbacks cmd_icb; /* cryptsetup CB structure */
  char* uuid;
} CryptSetupObject;

CryptSetupObject *this; /* hack because the #%^#$% has no user data pointer for CBs */

int yesDialog(char *msg)
{
  PyObject *result;
  PyObject *arglist;
  int res;
  int ok;

  if(this->yesDialogCB){
    arglist = Py_BuildValue("(s)", msg);
    if(!arglist) return 0;
    result = PyEval_CallObject(this->yesDialogCB, arglist);
    Py_DECREF(arglist);

    if (result == NULL) return 0;
    ok = PyArg_ParseTuple(result, "i", &res);
    if(!ok){
      res = 0;
    }

    Py_DECREF(result);
    return res;
  }
  else return 1;

  return 1;
}

void cmdLineLog(int cls, char *msg)
{
  PyObject *result;
  PyObject *arglist;

  if(this->cmdLineLogCB){
    arglist = Py_BuildValue("(is)", cls, msg);
    if(!arglist) return;
    result = PyEval_CallObject(this->cmdLineLogCB, arglist);
    Py_DECREF(arglist);
    Py_XDECREF(result);
  }
}

void uuidLog(int cls, char* uuid)
{
  if(!uuid || uuid[0]=='\n') return;
  if(this->uuid) free(this->uuid);
  this->uuid = strdup(uuid);
}

static void CryptSetup_dealloc(CryptSetupObject* self)
{
  /* free uuid cache */
  if(self->uuid) free(self->uuid);

  /* free the callbacks */
  Py_XDECREF(self->yesDialogCB);
  Py_XDECREF(self->cmdLineLogCB);
  /* free self */
  self->ob_type->tp_free((PyObject*)self);
}

static PyObject *CryptSetup_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
  CryptSetupObject *self;

  self = (CryptSetupObject *)type->tp_alloc(type, 0);
  if (self != NULL) {
    self->yesDialogCB = NULL;
    self->cmdLineLogCB = NULL;
    memset(&(self->cmd_icb), 0, sizeof(struct interface_callbacks));
    
    /* set the callback proxies */
    self->cmd_icb.yesDialog = &(yesDialog);
    self->cmd_icb.log = &(cmdLineLog);
    self->uuid = NULL;
  }

  return (PyObject *)self;
}

#define CryptSetup_HELP "CryptSetup object\n\
\n\
constructor takes two arguments:\n\
  __init__(yesDialog, logFunc)\n\
\n\
  yesDialog - python function with func(text) signature, which asks the user question text and returns 1 of the answer was positive or 0 if not\n\
  logFunc   - python function with func(level, text) signature to log stuff somewhere"

static int CryptSetup_init(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  PyObject *yesDialogCB=NULL, *cmdLineLogCB=NULL, *tmp=NULL;
  static char *kwlist[] = {"yesDialog", "logFunc", NULL};

  if (!PyArg_ParseTupleAndKeywords(args, kwds, "OO", kwlist, 
	&yesDialogCB, &cmdLineLogCB))
    return -1;

  if(yesDialogCB){
    tmp = self->yesDialogCB;
    Py_INCREF(yesDialogCB);
    self->yesDialogCB = yesDialogCB;
    Py_XDECREF(tmp);
  }
  
  if(cmdLineLogCB){
    tmp = self->cmdLineLogCB;
    Py_INCREF(cmdLineLogCB);
    self->cmdLineLogCB = cmdLineLogCB;
    Py_XDECREF(tmp);
  }

  return 0;
}

#define CryptSetup_askyes_HELP "Asks a question using the configured dialog CB\n\
\n\
  int askyes(message)"

static PyObject *CryptSetup_askyes(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"message", NULL};
  PyObject *message=NULL;
  PyObject *result;
  PyObject *arglist;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "O", kwlist, 
	&message))
    return NULL;

  Py_INCREF(message);
  arglist = Py_BuildValue("(O)", message);
  if(!arglist){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for internal call");
    return NULL;
  }
  result = PyEval_CallObject(self->yesDialogCB, arglist);
  Py_DECREF(arglist);
  Py_DECREF(message);

  return result;
}

#define CryptSetup_log_HELP "Logs a string using the configured log CB\n\
\n\
  log(int level, message)"

static PyObject *CryptSetup_log(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"priority", "message", NULL};
  PyObject *message=NULL;
  PyObject *priority=NULL;
  PyObject *result;
  PyObject *arglist;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "OO", kwlist, 
	&message, &priority))
    return NULL;

  Py_INCREF(message);
  Py_INCREF(priority);
  arglist = Py_BuildValue("(OO)", message, priority);
  if(!arglist){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for internal call");
    return NULL;
  }
  result = PyEval_CallObject(self->cmdLineLogCB, arglist);
  Py_DECREF(arglist);
  Py_DECREF(priority);
  Py_DECREF(message);

  return result;
}

#define CryptSetup_luksUUID_HELP "Get UUID of the LUKS device\n\
\n\
  luksUUID(device)"

static PyObject *CryptSetup_luksUUID(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"device", NULL};
  char* device=NULL;
  PyObject *result;
  int uuidres;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, 
	&device))
    return NULL;
  
  struct interface_callbacks cmd_icb; /* cryptsetup CB structure */
  cmd_icb.yesDialog = &(yesDialog);
  cmd_icb.log = &(uuidLog); /* stupid crappy API... */

  struct crypt_options co = {
    .device = device,
    .icb = &cmd_icb,
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  uuidres = crypt_luksUUID(&co);
  if(uuidres){
    PyErr_SetString(PyExc_RuntimeError, "Error getting UUID for device");
    return NULL;
  }

  result = Py_BuildValue("s", self->uuid);
  if(!result){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
    return NULL;
  }

  return result;
}

#define CryptSetup_isLuks_HELP "Is the device LUKS?\n\
\n\
  isLuks(device)\n\
\n\
  return value:\n\
   0   - device is LUKS\n\
  -22  - device is not LUKS"

static PyObject *CryptSetup_isLuks(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"device", NULL};
  char* device=NULL;
  PyObject *result;
  int is;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, 
	&device))
    return NULL;

  struct crypt_options co = {
    .device = device,
    .icb = &(self->cmd_icb),
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  is = crypt_isLuks(&co);

  result = Py_BuildValue("i", is);
  if(!result){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
    return NULL;
  }

  return result;
}

#define CryptSetup_luksStatus_HELP "What is the status of Luks subsystem?\n\
\n\
  luksStatus(name)\n\
\n\
  return value:\n\
  - dictionary with luks status\n\
  - or number <=0 if the device is not active"
  
static PyObject *CryptSetup_luksStatus(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"name", NULL};
  char* device=NULL;
  PyObject *result;
  int is;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, 
	&device))
    return NULL;

  struct crypt_options co = {
    .name = device,
    .icb = &(self->cmd_icb),
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  is = crypt_query_device(&co);

  if(is>0){
    result = Py_BuildValue("{s:s,s:s,s:s,s:i,s:s,s:K,s:K,s:K,s:s}",
	"dir", crypt_get_dir(),
	"name", co.name,
	"cipher", co.cipher,
	"keysize", co.key_size * 8,
	"device", co.device,
	"offset", co.offset,
	"size", co.size,
	"skip", co.skip,
	"mode", (co.flags & CRYPT_FLAG_READONLY) ? "readonly" : "read/write"
	);
    crypt_put_options(&co);

    if(!result){
      PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
      return NULL;
    }

  }
  else{
    result = Py_BuildValue("i", is);
    if(!result){
      PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
      return NULL;
    }
  }

  return result;
}

#define CryptSetup_luksFormat_HELP "Format device to enable LUKS\n\
\n\
  luksFormat(device, cipher = 'aes-cbc-essiv:sha256', keysize = 256, keyfile = None)\n\
\n\
  device - which device?\n\
  cipher - text string to specify cipher (cipher-mode-iv:hash_for_iv. probably aes-cbc-essiv:sha256 or aes-xts-plain)\n\
  keysize - key size in bits, cipher must support this\n\
  keyfile - filename which contains the key for encrypting this device. If None, cryptsetup will ask for one."

static PyObject *CryptSetup_luksFormat(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"device", "cipher", "keysize", "keyfile", NULL};
  char* device=NULL;
  char* cipher=NULL;
  char* keyfile=NULL;
  int keysize = 256;
  PyObject *keysize_object = NULL;
  PyObject *result;
  int is;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "s|zOz", kwlist, 
	&device, &cipher, &keysize_object, &keyfile))
    return NULL;

  if(!keysize_object || keysize_object==Py_None){
    //use default value
  } else if(!PyInt_Check(keysize_object)){
    PyErr_SetString(PyExc_TypeError, "keysize must be an integer");
    return NULL;
  } else if(PyInt_AsLong(keysize_object) % 8){
    PyErr_SetString(PyExc_TypeError, "keysize must have integer value dividable by 8");
    return NULL;
  } else if(PyInt_AsLong(keysize_object)<=0){
    PyErr_SetString(PyExc_TypeError, "keysize must be positive number bigger than 0");
    return NULL;
  } else{
    keysize = PyInt_AsLong(keysize_object);
  }


  if(!cipher) cipher="aes-cbc-essiv:sha256";

  struct crypt_options co = {
    .device = device,
    .key_size = keysize / 8, // in bytes, cipher must support this, for AES: 128,256 bits
    .key_slot = -1,
    .cipher = cipher, // cipher-mode-iv:hash_for_iv. probably aes-cbc-essiv:sha256 or aes-xts-plain
    .new_key_file = keyfile,
    .flags = 0,
    .iteration_time = 1000,
    .align_payload = 2048,
    .icb = &(self->cmd_icb),
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  is = crypt_luksFormat(&co);

  result = Py_BuildValue("i", is);
  if(!result){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
    return NULL;
  }

  return result;
}

#define CryptSetup_luksOpen_HELP "Open LUKS device and add it do devmapper\n\
\n\
  luksOpen(device, name, keyfile)\n\
\n\
  device - which device?\n\
  name - what mapping name should be created\n\
  keyfile - filename which contains the key for encrypting this device. '-' means standard input"

static PyObject *CryptSetup_luksOpen(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"device", "name", "keyfile", NULL};
  char* device=NULL;
  char* name=NULL;
  char* keyfile="-";
  PyObject *result;
  int is;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "ss|s", kwlist, 
	&device, &name, &keyfile))
    return NULL;

  struct crypt_options co = {
    .device = device,
    .name = name,
    .key_file = keyfile,
    .icb = &(self->cmd_icb),
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  is = crypt_luksOpen(&co);

  result = Py_BuildValue("i", is);
  if(!result){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
    return NULL;
  }

  return result;
}

#define CryptSetup_luksClose_HELP "Close LUKS device and remove it from devmapper\n\
\n\
  luksClose(name)\n\
\n\
  the mapping name which should be removed from devmapper."
static PyObject *CryptSetup_luksClose(CryptSetupObject* self, PyObject *args, PyObject *kwds)
{
  static char *kwlist[] = {"name", NULL};
  char* device=NULL;
  PyObject *result;
  int is;

  if (! PyArg_ParseTupleAndKeywords(args, kwds, "s", kwlist, 
	&device))
    return NULL;

  struct crypt_options co = {
    .name = device,
    .icb = &(self->cmd_icb),
  };

  /* hack, because the #&^#%& API has no user data pointer */
  this = self;

  is = crypt_remove_device(&co);

  result = Py_BuildValue("i", is);
  if(!result){
    PyErr_SetString(PyExc_RuntimeError, "Error during constructing values for return value");
    return NULL;
  }

  return result;
}

static PyMemberDef CryptSetup_members[] = {
  {"yesDialogCB", T_OBJECT_EX, offsetof(CryptSetupObject, yesDialogCB), 0, "confirmation dialog callback"},
  {"cmdLineLogCB", T_OBJECT_EX, offsetof(CryptSetupObject, cmdLineLogCB), 0, "logging callback"},
  {NULL}
};

static PyMethodDef CryptSetup_methods[] = {
  /* self-test methods */
  {"log", (PyCFunction)CryptSetup_log, METH_VARARGS|METH_KEYWORDS, CryptSetup_askyes_HELP},
  {"askyes", (PyCFunction)CryptSetup_askyes, METH_VARARGS|METH_KEYWORDS, CryptSetup_log_HELP},

  /* cryptsetup info entrypoints */
  {"luksUUID", (PyCFunction)CryptSetup_luksUUID, METH_VARARGS|METH_KEYWORDS, CryptSetup_luksUUID_HELP},
  {"isLuks", (PyCFunction)CryptSetup_isLuks, METH_VARARGS|METH_KEYWORDS, CryptSetup_isLuks_HELP},
  {"luksStatus", (PyCFunction)CryptSetup_luksStatus, METH_VARARGS|METH_KEYWORDS, CryptSetup_luksStatus_HELP},
  
  /* cryptsetup mgmt entrypoints */
  {"luksFormat", (PyCFunction)CryptSetup_luksFormat, METH_VARARGS|METH_KEYWORDS, CryptSetup_luksFormat_HELP},
  {"luksOpen", (PyCFunction)CryptSetup_luksOpen, METH_VARARGS|METH_KEYWORDS, CryptSetup_luksOpen_HELP},
  {"luksClose", (PyCFunction)CryptSetup_luksClose, METH_VARARGS|METH_KEYWORDS, CryptSetup_luksClose_HELP},

  {NULL}  /* Sentinel */
};

static PyTypeObject CryptSetupType = {
  PyObject_HEAD_INIT(NULL)
    0, /*ob_size*/
  "cryptsetup.CryptSetup", /*tp_name*/
  sizeof(CryptSetupObject),   /*tp_basicsize*/
  0,                         /*tp_itemsize*/
  (destructor)CryptSetup_dealloc,        /*tp_dealloc*/
  0,                         /*tp_print*/
  0,                         /*tp_getattr*/
  0,                         /*tp_setattr*/
  0,                         /*tp_compare*/
  0,                         /*tp_repr*/
  0,                         /*tp_as_number*/
  0,                         /*tp_as_sequence*/
  0,                         /*tp_as_mapping*/
  0,                         /*tp_hash */
  0,                         /*tp_call*/
  0,                         /*tp_str*/
  0,                         /*tp_getattro*/
  0,                         /*tp_setattro*/
  0,                         /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
  CryptSetup_HELP,     /* tp_doc */
  0,                   /* tp_traverse */
  0,	               /* tp_clear */
  0,                   /* tp_richcompare */
  0,	               /* tp_weaklistoffset */
  0,                   /* tp_iter */
  0,	               /* tp_iternext */
  CryptSetup_methods,             /* tp_methods */
  CryptSetup_members,             /* tp_members */
  0,                         /* tp_getset */
  0,                         /* tp_base */
  0,                         /* tp_dict */
  0,                         /* tp_descr_get */
  0,                         /* tp_descr_set */
  0,                         /* tp_dictoffset */
  (initproc)CryptSetup_init,      /* tp_init */
  0,                         /* tp_alloc */
  CryptSetup_new,                 /* tp_new */
};


static PyMethodDef cryptsetup_methods[] = {
      {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif

PyMODINIT_FUNC initcryptsetup(void)
{
  PyObject* m;
  if (PyType_Ready(&CryptSetupType) < 0)
    return;

  m = Py_InitModule3("cryptsetup", cryptsetup_methods, "CryptSetup pythonized API.");
  Py_INCREF(&CryptSetupType);
  PyModule_AddObject(m, "CryptSetup", (PyObject *)&CryptSetupType);
}

