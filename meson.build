project(
  'chronoledger',
  'c',
  version: '1.18.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'buildtype=release',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')
gnome = import('gnome')

add_project_arguments(
  '-DCHRONOLEDGER_VERSION="@0@"'.format(meson.project_version()),
  language: 'c',
)

gtk_dep = dependency('gtk4', version: '>= 4.12', include_type: 'system')
epoxy_dep = dependency('epoxy', include_type: 'system')
cairo_dep = dependency('cairo', include_type: 'system')
pangocairo_dep = dependency('pangocairo', include_type: 'system')
glib_dep = dependency('glib-2.0', version: '>= 2.74', include_type: 'system')
gio_dep = dependency('gio-2.0', version: '>= 2.74', include_type: 'system')
json_glib_dep = dependency('json-glib-1.0', version: '>= 1.6', include_type: 'system')
m_dep = cc.find_library('m', required: false)

dependencies = [
  gtk_dep,
  epoxy_dep,
  cairo_dep,
  pangocairo_dep,
  glib_dep,
  gio_dep,
  json_glib_dep,
  m_dep,
]

resource_sources = gnome.compile_resources(
  'chronoledger-resources',
  'data/chronoledger.gresource.xml',
  source_dir: 'data',
  c_name: 'chronoledger',
)

# glib-compile-resources emits one large C string for the bundled shaders.
# Keep warning-strict builds useful by limiting this standards-minimum warning
# exemption to generated code rather than weakening diagnostics project-wide.
resource_library = static_library(
  'chronoledger-resource-data',
  resource_sources,
  dependencies: gio_dep,
  c_args: ['-Wno-overlength-strings'],
)

sources = files(
  'src/main.c',
  'src/app_input.c',
  'src/app_dialogs.c',
  'src/app_runtime.c',
  'src/app_actions.c',
  'src/app_view.c',
  'src/ui_controls.c',
  'src/ui_layout.c',
  'src/cursor_state.c',
  'src/shader_profile.c',
  'src/renderer.c',
  'src/renderer_support.c',
  'src/renderer_upload.c',
  'src/renderer_scene.c',
  'src/renderer_cursor.c',
  'src/renderer_effects.c',
  'src/renderer_context.c',
  'src/renderer_shaders.c',
  'src/ui.c',
  'src/ui_scene.c',
  'src/ui_painter.c',
  'src/ui_focus_view.c',
  'src/ui_ledger_view.c',
  'src/ui_calibration_view.c',
  'src/ui_input.c',
  'src/chrono_model.c',
  'src/chrono_preferences.c',
  'src/chrono_document.c',
  'src/chrono_persistence.c',
  'src/time_parse.c',
  'src/power_policy.c',
  'src/render_policy.c',
)

executable(
  'chronoledger',
  sources,
  dependencies: dependencies,
  link_whole: resource_library,
  install: true,
)

subdir('tests')

install_data(
  'data/io.github.chronoledger.ChronoLedger.desktop',
  install_dir: get_option('datadir') / 'applications',
)
install_data(
  'data/io.github.chronoledger.ChronoLedger.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)
install_data(
  'data/icons/hicolor/scalable/apps/io.github.chronoledger.ChronoLedger.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps',
)
