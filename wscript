top = '.'
out = 'build'

import sys

def options(opt):
	opt.load('waf_unit_test')
	opt.load('compiler_cxx')

def configure(conf):
	conf.load('waf_unit_test')

	if not conf.env['CXX'] and sys.platform == "darwin":
		conf.env['CXX'] = 'clang++'
	conf.load('compiler_cxx')
	conf.env.append_unique('CXXFLAGS', ['-std=c++11', '-Wall', '-Wextra', '-g', '-O0'])
	if sys.platform == "darwin":
		# on darwin we force clang++ and libc++ at the moment as it's
		# the only option
		conf.env.CXXFLAGS_cxxshlib = ['-fPIC']
		conf.env.append_unique('CXXFLAGS', '-stdlib=libc++')
		conf.env.append_unique('LINKFLAGS', '-stdlib=libc++')

	def check_cfg(package, uselib_store):
		conf.check_cfg(
			package = package,
			args = '--cflags --libs',
			uselib_store = uselib_store,
		)

	check_cfg('lua', 'LUA')

def build(bld):
	bld.objects(
		source = 'interlua.cc',
		target = 'interlua',
	)
	bld.recurse('test')
