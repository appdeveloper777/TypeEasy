from setuptools import setup
from Cython.Build import cythonize

# Esto le dice a Cython que compile nuestro .pyx
# El resultado será 'motor_nlu_cython.so' (o .dll)
setup(
    ext_modules = cythonize("motor_nlu_cython.pyx")
)