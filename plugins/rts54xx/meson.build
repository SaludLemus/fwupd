cargs = ['-DG_LOG_DOMAIN="FuPluginRts54xx"']

install_data([
  'rts54xx.quirk',
  ],
  install_dir: join_paths(datadir, 'fwupd', 'quirks.d')
)

shared_module('fu_plugin_rts54xx',
  sources : [
    'fu-rts54xx-device.c',
    'fu-plugin-rts54xx.c',
  ],
  include_directories : [
    include_directories('../..'),
    include_directories('../../src'),
    include_directories('../../libfwupd'),
  ],
  install : true,
  install_dir: plugin_dir,
  c_args : cargs,
  dependencies : [
    plugin_deps,
  ],
)
