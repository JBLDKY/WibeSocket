from setuptools import setup, Extension
from setuptools import find_packages
import os

root = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
include_dir = os.path.join(root, 'include')
lib_dir = os.path.join(root, 'build')

ext = Extension(
    'wibesocket._core',
    sources=['wibesocket_module.c'],
    include_dirs=[include_dir],
    libraries=['wibesocket'],
    library_dirs=[os.path.join(lib_dir)],
)

setup(
    name='wibesocket',
    version='0.0.1',
    packages=find_packages(),
    package_dir={'wibesocket': 'wibesocket'},
    py_modules=['wibesocket_wrappers'],
    ext_modules=[ext],
)
