top = '.'
out = 'build'

import sys, platform
from waflib import Logs
from waflib.Errors import WafError
from waflib.Build import BuildContext
from waflib.Tools import waf_unit_test

# Lua emulation, maybe there is a better way? :D
class Dict(object):
	def __init__(self, d):
		self.__dict__ = d

def obj_to_dict(obj):
	return obj.__dict__

def dict_to_obj(d):
	return Dict(d)

def get_available_lua_pcs(conf):
	lua_potential_pcs = [
		'lua',
		'lua51', 'lua5.1',
		'lua52', 'lua5.2',
		'luajit',
	]
	available_lua_pcs = {}
	for pc in lua_potential_pcs:
		v = ""
		try:
			v = conf.cmd_and_log([conf.env.PKGCONFIG, '--modversion', pc]).strip()
		except WafError as e:
			pass
		if v:
			if v not in available_lua_pcs:
				available_lua_pcs[v] = pc

	return available_lua_pcs

def get_51_52_jit(pcs):
	out = []
	for k, v in pcs.items():
		item = Dict({})
		if v == "luajit":
			item.name = "LuaJIT"
		else:
			item.name = "Lua"
		item.version = k
		item.pc = v

		if k.startswith("2.") and v == "luajit":
			item.uselib = "LUAJIT"
		elif k.startswith("5.1"):
			item.uselib = "LUA51"
		elif k.startswith("5.2"):
			item.uselib = "LUA52"
		else:
			continue

		out.append(item)

	out.sort(key=lambda x: x.uselib)
	return out

def options(opt):
	opt.load('waf_unit_test')
	opt.load('compiler_cxx')
	opt.add_option(
		'--onelua',
		action = 'store_true',
		default = False,
		help = 'Build tests using only default lua on this machine',
	)
	opt.add_option(
		'--optimize',
		action = 'store_true',
		default = False,
		help = "Don't add debug info, optimize code harder instead",
	)

def configure(conf):
	conf.load('waf_unit_test')

	if not conf.env['CXX'] and sys.platform == "darwin":
		conf.env['CXX'] = 'clang++'
	conf.load('compiler_cxx')
	if conf.options.optimize:
		# we don't need debug flags for benchmarks
		conf.env.append_unique('CXXFLAGS', ['-std=c++11', '-Wall', '-Wextra', '-O3'])
	else:
		conf.env.append_unique('CXXFLAGS', ['-std=c++11', '-Wall', '-Wextra', '-g', '-O0'])
	if sys.platform == "darwin":
		# on darwin we force clang++ and libc++ at the moment as it's
		# the only option
		conf.env.CXXFLAGS_cxxshlib = ['-fPIC']
		conf.env.append_unique('CXXFLAGS', '-stdlib=libc++')
		conf.env.append_unique('LINKFLAGS', '-stdlib=libc++')
	conf.check_cfg(atleast_pkgconfig_version='0.0')

	conf.start_msg("Checking for available Lua versions")
	luas = get_51_52_jit(get_available_lua_pcs(conf))
	conf.env.LUAS = [obj_to_dict(x) for x in luas]
	if luas:
		conf.end_msg(", ".join(["%s %s" % (x.name, x.version) for x in luas]))
	else:
		conf.end_msg("no", "YELLOW")
		conf.fatal("No Lua versions found, InterLua tries the following pkg-config " +
			"packages: lua, lua51, lua5.1, lua52, lua5.2, luajit")

	if conf.options.onelua:
		onelua = None
		for l in luas:
			if l.pc == 'lua':
				onelua = l

		if not onelua:
			conf.fatal("Default Lua version is not found")

		conf.env.LUAS = [obj_to_dict(onelua)]
		luas = [onelua]

	def check_cfg(package, uselib_store, msg):
		conf.check_cfg(
			package = package,
			args = '--cflags --libs',
			uselib_store = uselib_store,
			msg = msg,
		)

	for l in luas:
		check_cfg(l.pc, l.uselib, "Checking for %s %s" % (l.name, l.version))

	if sys.platform == "darwin" and	platform.machine() == 'x86_64' and conf.env.LIB_LUAJIT:
		conf.env.append_unique('LINKFLAGS_LUAJIT', [
			'-pagezero_size 10000',
			'-image_base 100000000',
		])

def summary(bld):
	lst = getattr(bld, 'utest_results', [])
	if lst:
		Logs.pprint('CYAN', '=== EXECUTION SUMMARY ===')

		total = len(lst)
		tfail = len([x for x in lst if x[1]])

		for (f, code, out, err) in lst:
			Logs.pprint('CYAN', '%s' % f)
			out = (out.rstrip() + "\n".encode("utf-8") + err.rstrip()).strip()
			if out:
				Logs.pprint('NORMAL', '%s' % out.decode('utf-8'))
		col = 'CYAN'
		if tfail:
			col = 'RED'
		Logs.pprint(col, 'tests that pass %d/%d' % (total-tfail, total))

def queue_tests_summary(bld):
	bld.add_post_fun(summary)
	bld.add_post_fun(waf_unit_test.set_exit_code)


def build_interlua(bld):
	bld.luas = [dict_to_obj(x) for x in bld.env.LUAS]
	for l in bld.luas:
		bld.objects(
			source = 'interlua.cc',
			target = 'interlua_' + l.uselib.lower(),
			use = l.uselib,
		)

def build(bld):
	queue_tests_summary(bld)
	build_interlua(bld)
	bld.recurse('test')

#-----------------------------------------------------------------------

class Bench(BuildContext):
	cmd = 'bench'
	fun = 'bench'

def bench(bld):
	queue_tests_summary(bld)
	build_interlua(bld)
	bld.recurse('bench')
