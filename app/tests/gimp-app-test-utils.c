/* GIMP - The GNU Image Manipulation Program
 * Copyright (C) 2009 Martin Nordholts <martinn@src.gnome.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <glib.h>

#include "gimp-app-test-utils.h"


static void
gimp_test_utils_set_env_to_subdir (const gchar *root_env_var,
                                   const gchar *subdir,
                                   const gchar *target_env_var)
{
  const gchar *root_dir   = NULL;
  gchar       *target_dir = NULL;

  /* Get root dir */
  root_dir = g_getenv (root_env_var);
  if (! root_dir)
    g_printerr ("The env var %s is not set, you are probably running\n"
                "in a debugger. Set it manually, e.g.:\n"
                "set env %s=%s/source/gimp\n",
                root_env_var,
                root_env_var, g_get_home_dir ());

  /* Construct path and setup target env var */
  target_dir = g_build_filename (root_dir, subdir, NULL);
  g_setenv (target_env_var, target_dir, TRUE);
  g_free (target_dir);
}


/**
 * gimp_test_utils_set_gimp2_directory:
 * @root_env_var: Either "GIMP_TESTING_ABS_TOP_SRCDIR" or
 *                "GIMP_TESTING_ABS_TOP_BUILDDIR"
 * @subdir:       Subdir, may be %NULL
 *
 * Sets GIMP2_DIRECTORY to the source dir @root_env_var/@subdir. The
 * environment variables is set up by the test runner, see Makefile.am
 **/
void
gimp_test_utils_set_gimp2_directory (const gchar *root_env_var,
                                     const gchar *subdir)
{
  gimp_test_utils_set_env_to_subdir (root_env_var,
                                     subdir,
                                     "GIMP2_DIRECTORY" /*target_env_var*/);
}

/**
 * gimp_test_utils_setup_menus_dir:
 *
 * Sets GIMP_TESTING_MENUS_DIR to "$top_srcdir/menus".
 **/
void
gimp_test_utils_setup_menus_dir (void)
{
  /* GIMP_TESTING_ABS_TOP_SRCDIR is set by the automake test runner,
   * see Makefile.am
   */
  gimp_test_utils_set_env_to_subdir ("GIMP_TESTING_ABS_TOP_SRCDIR" /*root_env_var*/,
                                     "menus" /*subdir*/,
                                     "GIMP_TESTING_MENUS_DIR" /*target_env_var*/);
}