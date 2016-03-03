/*
 *  OpenBangla Keyboard
 *  Copyright (C) 2015-2016 Muhammad Mominul Huque <mominul2082@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* iBus IM Engine */

#include <ibus.h>
#include <glib.h>
#include "im.h"
#include "ibus_keycode.h"
#include "Layout.h"
#include "log.h"

static IBusBus *bus = NULL;
static IBusFactory *factory = NULL;
static IBusEngine *engine = NULL;
IBusLookupTable *table = NULL;
gint id = 0;
guint candidateSel = 0;

bool onlyPreedit;

void ibus_disconnected_cb(IBusBus *bus, gpointer user_data) {
  ibus_quit();
  LOG_INFO("[IM:iBus]: Bus disconnected!\n");
}

gboolean ibus_process_key_event_cb(IBusEngine *engine,
                                   guint       keyval,
                                   guint       keycode,
                                   guint       state) {
  // Set Defaults
  bool kshift, kctrl, kalt;
  kshift = false;
  kctrl = false;
  kalt = false;

  // Don't accept Key Release event
  if (state & IBUS_RELEASE_MASK) return FALSE;

  // Set modifiers
  if(state & IBUS_SHIFT_MASK) kshift = true;
  if(state & IBUS_CONTROL_MASK) kctrl = true;
  if(state & IBUS_MOD1_MASK) kalt = true;

  // Send the key to layout management
  bool ret = gLayout->sendKey(ibus_keycode(keyval), kshift, kctrl, kalt);
  LOG_DEBUG("[IM:iBus]: Layout Management %s the event\n",ret?"accepted":"rejected");
  return (gboolean)ret;
}

void ibus_enable_cb(IBusEngine *engine) {
  LOG_INFO("[IM:iBus]: IM enabled\n");
}

void ibus_disable_cb(IBusEngine *engine) {
  LOG_INFO("[IM:iBus]: IM disabled\n");
}

IBusEngine* ibus_create_engine_cb(IBusFactory *factory,
                                  gchar* engine_name,
                                  gpointer     user_data) {
  id += 1;
  gchar *path = g_strdup_printf("/org/freedesktop/IBus/Engine/%i",id);
  engine = ibus_engine_new( engine_name,
                            path,
                            ibus_bus_get_connection(bus) );

  // Setup Lookup table
  table = ibus_lookup_table_new (9, 0, TRUE, TRUE);
  ibus_lookup_table_set_orientation(table, IBUS_ORIENTATION_HORIZONTAL);
  g_object_ref_sink (table);

  LOG_INFO("[IM:iBus]: Creating IM Engine\n");
  LOG_DEBUG("[IM:iBus]: Creating IM Engine with name:%s and id:%d\n",(char*)engine_name, id);

  g_signal_connect(engine, "process-key-event", G_CALLBACK(ibus_process_key_event_cb), NULL);
  g_signal_connect(engine, "enable", G_CALLBACK(ibus_enable_cb), NULL);
  g_signal_connect(engine, "disable", G_CALLBACK(ibus_disable_cb), NULL);

  return engine;
}

void start_setup(bool ibus) {
  IBusComponent *component;

  ibus_init();

  bus = ibus_bus_new();
  g_signal_connect(bus, "disconnected", G_CALLBACK (ibus_disconnected_cb), NULL);

  factory = ibus_factory_new(ibus_bus_get_connection(bus));
  g_signal_connect(factory, "create-engine", G_CALLBACK(ibus_create_engine_cb), NULL);

  if(ibus) {
    ibus_bus_request_name(bus, "org.freedesktop.IBus.OpenBangla", 0);
  } else {
    component = ibus_component_new( "org.freedesktop.IBus.OpenBangla",
                                    "OpenBangla Keyboard",
                                    "1.0",
                                    "GPL 3",
                                    "See AboutBox",
                                    "http://openbangla.github.io",
                                    LIBEXEC_DIR "/OpenBangla-Engine --ibus",
                                    "openbangla-keyboard"
                                  );

    ibus_component_add_engine(component,
                              ibus_engine_desc_new( "OpenBangla",
                                                    "OpenBangla Keyboard",
                                                    "OpenBangla Keyboard IME for iBus",
                                                    "bn",
                                                    "GPL 3",
                                                    "See AboutBox",
                                                    PKGDATADIR "/icons/OpenBangla-Keyboard.png",
                                                    "us"
                                                  ));
    ibus_bus_register_component(bus, component);
  }
  ibus_main();
}

void ibus_update_preedit() {
  if(!onlyPreedit) {
    ibus_lookup_table_set_cursor_pos(table, candidateSel);
    ibus_engine_update_lookup_table_fast(engine, table, TRUE);
  }
  // Get current suggestion
  IBusText *txt = ibus_lookup_table_get_candidate(table, candidateSel);
  ibus_engine_update_preedit_text(engine, txt, ibus_text_get_length(txt), TRUE);
}

void im_table_sel_inc() {
  guint lastIndex = ibus_lookup_table_get_number_of_candidates(table) -1;
  if((candidateSel + 1) > lastIndex) {
    candidateSel = -1;
  }
  ++candidateSel;
  ibus_update_preedit();
}

void im_table_sel_dec() {
  if(candidateSel ==  0) {
    candidateSel = ibus_lookup_table_get_number_of_candidates(table) -1;
    ibus_update_preedit();
    return;
  } else {
    --candidateSel;
    ibus_update_preedit();
  }
}

void im_update_suggest(std::vector<std::string> lst, std::string typed) {
  // Update auxiliary text
  IBusText *caux = ibus_text_new_from_string((gchar*)typed.c_str());
  ibus_engine_update_auxiliary_text(engine, caux, TRUE);
  ibus_lookup_table_clear(table); // At first, remove all candidates
  for(auto& str : lst) {
    IBusText *ctext = ibus_text_new_from_string((gchar*)str.c_str());
    ibus_lookup_table_append_candidate(table, ctext);
    // Hide candidate labels // Hack learned from ibus-avro
    IBusText *clabel = ibus_text_new_from_string("");
    ibus_lookup_table_append_label(table, clabel);
  }
  candidateSel = 0;
  onlyPreedit = false;
  ibus_update_preedit();
}

void im_update(std::string text) {
  ibus_lookup_table_clear(table); // At first, remove all candidates
  IBusText *ctext = ibus_text_new_from_string((gchar*)text.c_str());
  ibus_lookup_table_append_candidate(table, ctext);
  candidateSel = 0;
  onlyPreedit = true;
  ibus_update_preedit();
}

void im_reset() {
  // Reset all our mess
  onlyPreedit = false;
  candidateSel = 0;
  ibus_lookup_table_clear(table);
  ibus_engine_hide_preedit_text(engine);
  ibus_engine_hide_auxiliary_text(engine);
  ibus_engine_hide_lookup_table(engine);
}

std::string im_get_selection(int index) {
  IBusText *txt = ibus_lookup_table_get_candidate(table, (guint)index);
  std::string ret = (char*)txt->text;
  return ret;
}

int im_get_selection_id() {
  return (int)candidateSel;
}

void im_selectCandidate(int index) {
  candidateSel = (guint)index;
  ibus_update_preedit();
}

void im_commit() {
  IBusText *txt = ibus_lookup_table_get_candidate(table, candidateSel);
  ibus_engine_commit_text(engine,txt);
  im_reset();
  candidateSel = 0;
}

void im_start(bool executed) {
  LOG_DEBUG("[IM:iBus]: Started IM facilities.\n");
  start_setup(executed);
}
