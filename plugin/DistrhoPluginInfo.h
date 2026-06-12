#pragma once

#define DISTRHO_PLUGIN_BRAND       "Dubplex"
#define DISTRHO_PLUGIN_NAME        "DBT NJD Siren-1"
#define DISTRHO_PLUGIN_URI         "https://github.com/LeoFabre/njd-siren-dpf"
#define DISTRHO_PLUGIN_CLAP_ID     "fr.dubplex.dbt-njd-siren-1"
#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2
#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#ifndef DISTRHO_PLUGIN_HAS_UI
  #define DISTRHO_PLUGIN_HAS_UI    0   /* CMake overrides this when UI is built */
#endif
#define DISTRHO_PLUGIN_WANT_PROGRAMS   0
#define DISTRHO_PLUGIN_WANT_STATE      0
#define DISTRHO_PLUGIN_WANT_TIMEPOS    0
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT 0   /* trigger arrives as a CC->param mapping in Sushi */

#define DISTRHO_UI_USE_NANOVG     1
#define DISTRHO_UI_USER_RESIZABLE 1
#define DISTRHO_UI_DEFAULT_WIDTH  740
#define DISTRHO_UI_DEFAULT_HEIGHT 700
