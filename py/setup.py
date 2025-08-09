from setuptools import setup, Extension
import os

root = os.path.abspath(os.path.join(os.path.dirname(__file__), os.pardir))
include_dir = os.path.join(root, 'include')
lib_dir = os.path.join(root, 'build')

ext = Extension(
    'wibesocket',
    sources=['wibesocket_module.c'],
    include_dirs=[include_dir],
    libraries=['wibesocket'],
    library_dirs=[os.path.join(lib_dir)],
)

setup(
    name='wibesocket',
    version='0.0.1',
    ext_modules=[ext],
)
