# bluealsa-hfpag-plugin - meson.build
# SPDX-FileCopyrightText: 2016-2025 @borine <https://github.com/borine/>
# SPDX-License-Identifier: MIT

project('bluealsa-hfpag-plugin', 'c', version: '0.0.4', license: 'MIT')

prefix = get_option('prefix')
assert(prefix.startswith('/'), 'Prefix is not absolute: "@0@"'.format(prefix))

alsaconfdir = '/etc/alsa/conf.d'

conf_data = configuration_data()
conf_data.set('prefix', prefix)

compiler = meson.get_compiler('c')

alsa_dep = dependency('alsa', version: '>= 1.2.5')
dbus_dep = dependency('dbus-1')

alsa_plugin_dir = join_paths(
	alsa_dep.get_variable(pkgconfig : 'libdir'),
	'alsa-lib'
)

hfp_ag_plugin_sources = [
	'hfpag-hook.c',
	'hfpag-session.c',
	'bluez-alsa/dbus-client.c',
	'bluez-alsa/dbus-client-pcm.c',
	'bluez-alsa/dbus-client-rfcomm.c',
	'bluez-alsa/a2dp-codecs.c',
]

hfp_ag_plugin = shared_library(
	'asound_module_pcm_hooks_bluealsa_hfpag',
	hfp_ag_plugin_sources,
	dependencies: [ alsa_dep, dbus_dep ],
	c_args: '-DPIC',
	install: true,
	install_dir: alsa_plugin_dir,
)

install_data(
	'21-bluealsa-hfpag.conf',
	install_dir: alsaconfdir,
)
