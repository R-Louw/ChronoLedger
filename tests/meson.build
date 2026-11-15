core_tests = executable(
  'chronoledger-core-tests',
  files(
    'test_core.c',
    'test_persistence.c',
    'test_timing.c',
    'test_ordering.c',
    '../src/chrono_model.c',
    '../src/chrono_document.c',
    '../src/chrono_preferences.c',
    '../src/chrono_persistence.c',
    '../src/chrono_json.c',
    '../src/time_parse.c',
    '../src/power_policy.c',
    '../src/render_policy.c',
    '../src/ui_layout.c',
    '../src/shader_profile.c',
    '../src/app_view.c',
    '../src/renderer_shaders.c',
  ),
  include_directories: include_directories('../src'),
  dependencies: [gtk_dep, glib_dep, gio_dep, json_glib_dep, m_dep],
  link_whole: resource_library,
)

test(
  'core contracts',
  core_tests,
  env: ['G_DEBUG=fatal-warnings,fatal-criticals'],
  protocol: 'tap',
)
