from distutils.core import setup, Extension
setup(name="pycryptsetup",
              version = '0.0.11',
              description = "Python bindings for cryptsetup",
              author = "Martin Sivak",
              author_email= "msivak@redhat.com",
              license = 'GPLv2+',
              packages = ["pycryptsetup"],
              ext_modules = [Extension("cryptsetup", ["pycryptsetup/cryptsetup.c"], library_dirs=['.'], libraries=['cryptsetup'])]
              )

