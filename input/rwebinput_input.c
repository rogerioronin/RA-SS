/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2010-2014 - Michael Lelli
 * 
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "input_common.h"

#include "../driver.h"

#include <boolean.h>
#include "../general.h"
#include "keyboard_line.h"

#include "../emscripten/RWebInput.h"

static bool uninited = false;

typedef struct rwebinput_input
{
   rwebinput_state_t state;
   int context;
} rwebinput_input_t;

static void *rwebinput_input_init(void)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)calloc(1, sizeof(*rwebinput));
   if (!rwebinput)
      return NULL;

   rwebinput->context = RWebInputInit();
   if (!rwebinput->context)
   {
      free(rwebinput);
      return NULL;
   }

   input_init_keyboard_lut(rarch_key_map_rwebinput);

   return rwebinput;
}

static bool rwebinput_key_pressed(rwebinput_input_t *rwebinput, int key)
{
   if (key >= RETROK_LAST)
      return false;

   unsigned sym = input_translate_rk_to_keysym((enum retro_key)key);
   bool ret = rwebinput->state.keys[sym >> 3] & (1 << (sym & 7));
   return ret;
}

static bool rwebinput_is_pressed(rwebinput_input_t *rwebinput, const struct retro_keybind *binds, unsigned id)
{
   if (id < RARCH_BIND_LIST_END)
   {
      const struct retro_keybind *bind = &binds[id];
      return bind->valid && rwebinput_key_pressed(rwebinput, binds[id].key);
   }
   else
      return false;
}

static bool rwebinput_bind_button_pressed(void *data, int key)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;
   return rwebinput_is_pressed(rwebinput, g_settings.input.binds[0], key);
}

static int16_t rwebinput_mouse_state(rwebinput_input_t *rwebinput, unsigned id)
{
   switch (id)
   {
      case RETRO_DEVICE_ID_MOUSE_X:
         return (int16_t) rwebinput->state.mouse_x;
      case RETRO_DEVICE_ID_MOUSE_Y:
         return (int16_t) rwebinput->state.mouse_y;
      case RETRO_DEVICE_ID_MOUSE_LEFT:
         return rwebinput->state.mouse_l;
      case RETRO_DEVICE_ID_MOUSE_RIGHT:
         return rwebinput->state.mouse_r;
      default:
         return 0;
   }
}

static int16_t rwebinput_analog_pressed(rwebinput_input_t *rwebinput,
      const struct retro_keybind *binds, unsigned idx, unsigned id)
{
   unsigned id_minus = 0;
   unsigned id_plus  = 0;
   input_conv_analog_id_to_bind_id(idx, id, &id_minus, &id_plus);

   int16_t pressed_minus = rwebinput_is_pressed(rwebinput, binds, id_minus) ? -0x7fff : 0;
   int16_t pressed_plus = rwebinput_is_pressed(rwebinput, binds, id_plus) ? 0x7fff : 0;
   return pressed_plus + pressed_minus;
}

static int16_t rwebinput_input_state(void *data, const struct retro_keybind **binds,
      unsigned port, unsigned device, unsigned idx, unsigned id)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;

   switch (device)
   {
      case RETRO_DEVICE_JOYPAD:
         return rwebinput_is_pressed(rwebinput, binds[port], id);

      case RETRO_DEVICE_ANALOG:
         return rwebinput_analog_pressed(rwebinput, binds[port], idx, id);

      case RETRO_DEVICE_KEYBOARD:
         return rwebinput_key_pressed(rwebinput, id);

      case RETRO_DEVICE_MOUSE:
         return rwebinput_mouse_state(rwebinput, id);

      default:
         return 0;
   }
}

static void rwebinput_input_free(void *data)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;
   uninited = true;

   if (!rwebinput)
      return;

   RWebInputDestroy(rwebinput->context);

   free(rwebinput);
}

static void rwebinput_input_poll(void *data)
{
   rwebinput_input_t *rwebinput = (rwebinput_input_t*)data;

   rwebinput_state_t *state = RWebInputPoll(rwebinput->context);

   // get new keys
   for (unsigned i = 0; i < 32; i++)
   {
      if (state->keys[i] != rwebinput->state.keys[i])
      {
         uint8_t diff = state->keys[i] ^ rwebinput->state.keys[i];
         for (unsigned k = 0; diff; diff >>= 1, k++)
         {
            if (diff & 1)
            {
               input_keyboard_event((state->keys[i] & (1 << k)) != 0, input_translate_keysym_to_rk(i * 8 + k), 0, 0);
            }
         }
      }
   }

   memcpy(&rwebinput->state, state, sizeof(rwebinput->state));
}

static void rwebinput_grab_mouse(void *data, bool state)
{
   (void)data;
   (void)state;
}

static uint64_t rwebinput_get_capabilities(void *data)
{
   uint64_t caps = 0;

   caps |= (1 << RETRO_DEVICE_JOYPAD);
   caps |= (1 << RETRO_DEVICE_ANALOG);
   caps |= (1 << RETRO_DEVICE_KEYBOARD);
   caps |= (1 << RETRO_DEVICE_MOUSE);

   return caps;
}

input_driver_t input_rwebinput = {
   rwebinput_input_init,
   rwebinput_input_poll,
   rwebinput_input_state,
   rwebinput_bind_button_pressed,
   rwebinput_input_free,
   NULL,
   NULL,
   rwebinput_get_capabilities,
   "rwebinput",
   rwebinput_grab_mouse,
};
