/*
 * Copyright (c) 2000, Red Hat, Inc.
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     A copy of the GNU General Public License can be found at
 *     http://www.gnu.org/
 *
 * Written by DJ Delorie <dj@cygnus.com>
 *
 */

/* The purpose of this file is to manage all the desktop setup, such
   as start menu, batch files, desktop icons, and shortcuts.  Note
   that unlike other do_* functions, this one is called directly from
   install.cc */

static char *cvsid = "\n%%% $Id$\n";

#include "win32.h"
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "resource.h"
#include "ini.h"
#include "msg.h"
#include "state.h"
#include "concat.h"
#include "mkdir.h"
#include "dialog.h"
#include "version.h"

#include "port.h"

extern "C" {
  void make_link_2 (char *exepath, char *args, char *icon, char *lname);
};

static OSVERSIONINFO verinfo;

/* Lines starting with '@' are conditionals - include 'N' for NT,
   '5' for Win95, '8' for Win98, '*' for all, like this:
	echo foo
	@N8
	echo NT or 98
	@*
   */

static char *etc_profile[] = {
  "PATH=\"/usr/local/bin:/usr/bin:/bin:$PATH\"",
  "unset DOSDRIVE",
  "unset DOSDIR",
  "unset TMPDIR",
  "unset TMP",
  "",
  "USER=\"`id -un`\"",
  "",
  "# Set up USER's home directory",
  "if [ -z \"$HOME\" ]; then",
  "  HOME=\"/home/$USER\"",
  "fi",
  "",
  "if [ ! -d \"$HOME\" ]; then",
  "  mkdir -p \"$HOME\"",
  "fi",
  "",
  "export HOME USER",
  "",
  "for i in /etc/profile.d/*.sh ; do",
  "  if [ -f $i ]; then",
  "    . $i",
  "  fi",
  "done",
  "",
  "export MAKE_MODE=unix",
  "export PS1='\\[\\033]0;\\w\\007",
  "\\033[32m\\]\\u@\\h \\[\\033[33m\\w\\033[0m\\]",
  "$ '",
  "",
  "cd \"$HOME\"",
  "",
  "test -f ./.bashrc && . ./.bashrc",
  0
};

#define COMMAND9XARGS "/E:4096 /c"
#define COMMAND9XEXE  "\\command.com"

static char *batname;
static char *iconname;

static void
make_link (char *linkpath, char *title, char *target)
{
  char argbuf[_MAX_PATH];
  char *fname = concat (linkpath, "/", title, ".lnk", 0);

  if (_access (fname, 0) == 0)
    return; /* already exists */

  msg ("make_link %s, %s, %s\n", fname, title, target);

  mkdir_p (0, fname);

  char *exepath, *args;

  /* If we are running Win9x, build a command line. */
  if (verinfo.dwPlatformId == VER_PLATFORM_WIN32_NT)
    {
      exepath = target;
      args = "";
    }
  else
    {
      char *pccmd;
      char windir[MAX_PATH];

      GetWindowsDirectory (windir, sizeof (windir));
      exepath = concat (windir, COMMAND9XEXE, 0);
      sprintf (argbuf, "%s %s", COMMAND9XARGS, target);
      args = argbuf;
    }

  msg ("make_link_2 (%s, %s, %s, %s)", exepath, args, iconname, fname);
  make_link_2 (exepath, args, iconname, fname);
}

static void
start_menu (char *title, char *target)
{
  char path[_MAX_PATH];
  LPITEMIDLIST id;
  int issystem = (root_scope == IDC_ROOT_SYSTEM) ? 1 : 0;
  SHGetSpecialFolderLocation (NULL, issystem ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS, &id);
  SHGetPathFromIDList (id, path);
// following lines added because it appears Win95 does not use common programs
// unless it comes into play when multiple users for Win95 is enabled
  msg("Program directory for program link: %s",path);
  if ( strlen(path) == 0) {
     SHGetSpecialFolderLocation (NULL, CSIDL_PROGRAMS, &id);
     SHGetPathFromIDList (id, path);
     msg("Program directory for program link changed to: %s",path);
  }
// end of Win95 addition
  strcat (path, "/Cygnus Solutions");
  make_link (path, title, target);
}

static void
desktop_icon (char *title, char *target)
{
  char path[_MAX_PATH];
  LPITEMIDLIST id;
  int issystem = (root_scope == IDC_ROOT_SYSTEM) ? 1 : 0;
  //SHGetSpecialFolderLocation (NULL, issystem ? CSIDL_DESKTOP : CSIDL_COMMON_DESKTOPDIRECTORY, &id);
  SHGetSpecialFolderLocation (NULL, issystem ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY, &id);
  SHGetPathFromIDList (id, path);
// following lines added because it appears Win95 does not use common programs
// unless it comes into play when multiple users for Win95 is enabled
  msg("Desktop directory for desktop link: %s",path);
  if ( strlen(path) == 0) {
     SHGetSpecialFolderLocation (NULL, CSIDL_DESKTOPDIRECTORY, &id);
     SHGetPathFromIDList (id, path);
     msg("Desktop directory for deskop link changed to: %s",path);
  }
// end of Win95 addition
  make_link (path, title, target);
}

static void
make_cygwin_bat ()
{
  batname = backslash (concat (root_dir, "/cygwin.bat", 0));

  /* if the batch file exists, don't overwrite it */
  if (_access (batname, 0) == 0)
    return;

  FILE *bat = fopen (batname, "wt");
  if (!bat)
    return;

  fprintf (bat, "@echo off\n\n");

  fprintf (bat, "%.2s\n", root_dir);
  fprintf (bat, "chdir %s\n\n", backslash (concat (root_dir+2, "/bin", 0)));

  fprintf (bat, "bash --login -i\n");

  fclose (bat);
}

static void
make_etc_profile ()
{
  char *fname = concat (root_dir, "/etc/profile", 0);

  /* if the file exists, don't overwrite it */
  if (_access (fname, 0) == 0)
    return;

  char os;
  switch (verinfo.dwPlatformId)
    {
      case VER_PLATFORM_WIN32_NT:
	os = 'N';
	break;
      case VER_PLATFORM_WIN32_WINDOWS:
	if (verinfo.dwMinorVersion == 0)
	  os = '5';
	else
	  os = '8';
	break;
      default:
	os = '?';
	break;
    }
  msg ("os is %c", os);

  FILE *p = fopen (fname, "wb");
  if (!p)
    return;

  int i, allow=1;
  for (i=0; etc_profile[i]; i++)
    {
      if (etc_profile[i][0] == '@')
	{
	  allow = 0;
	  msg ("profile: %s", etc_profile[i]);
	  for (char *cp = etc_profile[i]+1; *cp; cp++)
	    if (*cp == os || *cp == '*')
	      allow = 1;
	  msg ("allow is %d\n", allow);
	}
      else if (allow)
	fprintf (p, "%s\n", etc_profile[i]);
    }

  fclose (p);
}

static int
uexists (char *path)
{
  char *f = concat (root_dir, path, 0);
  int a = _access (f, 0);
  free (f);
  if (a == 0)
    return 1;
  return 0;
}

static void
make_passwd_group ()
{
  if (verinfo.dwPlatformId != VER_PLATFORM_WIN32_NT)
    {
      int i;

      LOOP_PACKAGES
	{
    if (!strcmp (package[i].name, "cygwin") &&
       package[i].action != ACTION_SRC_ONLY)
	    {
	      /* mkpasswd and mkgroup are not working on 9x/ME up to 1.1.5-4 */
	      char *border_version = canonicalize_version ("1.1.5-4");
	      char *inst_version = canonicalize_version (pi.version);

	      if (strcmp (inst_version, border_version) <= 0)
		return;

	      break;
	    }
	}
    }

  if (uexists ("/etc/passwd") && uexists ("/etc/group"))
    return;

  char *fname = concat (root_dir, "/etc/postinstall/passwd-grp.bat", 0);
  mkdir_p (0, fname);

  FILE *p = fopen (fname, "wb");
  if (!p)
    return;

  if (!uexists ("/etc/passwd"))
    fprintf (p, "bin\\mkpasswd -l > etc\\passwd\n");
  if (!uexists ("/etc/group"))
    fprintf (p, "bin\\mkgroup -l > etc\\group\n");

  fclose (p);
}

static void
save_icon ()
{
  iconname = backslash (concat (root_dir, "/cygwin.ico", 0));

  HRSRC rsrc = FindResource (NULL, "CYGWIN.ICON", "FILE");
  if (rsrc == NULL)
    {
      fatal ("FindResource failed");
    }
  HGLOBAL res = LoadResource (NULL, rsrc);
  char *data = (char *) LockResource (res);
  int len = SizeofResource (NULL, rsrc);

  FILE *f = fopen (iconname, "wb");
  if (f)
    {
      fwrite (data, 1, len, f);
      fclose (f);
    }
}

static void
do_desktop_setup()
{
  save_icon ();

  make_cygwin_bat ();
  make_etc_profile ();
  make_passwd_group ();

  if (root_menu) {
    start_menu ("Cygwin Bash Shell", batname);
  }

  if (root_desktop) {
    desktop_icon ("Cygwin", batname);
  }
}

static int da[] = { IDC_ROOT_DESKTOP, 0 };
static int ma[] = { IDC_ROOT_MENU, 0 };

static void
check_if_enable_next (HWND h)
{
  EnableWindow (GetDlgItem (h, IDOK), 1);
}

static void
load_dialog (HWND h)
{
  rbset (h, da, root_desktop);
  rbset (h, ma, root_menu);
  check_if_enable_next (h);
}

static int check_desktop (char *title, char *target)
{
  char path[_MAX_PATH];
  LPITEMIDLIST id;
  int issystem = (root_scope == IDC_ROOT_SYSTEM) ? 1 : 0;
  SHGetSpecialFolderLocation (NULL, issystem ? CSIDL_COMMON_DESKTOPDIRECTORY : CSIDL_DESKTOPDIRECTORY, &id);
  SHGetPathFromIDList (id, path);
  // following lines added because it appears Win95 does not use common programs
  // unless it comes into play when multiple users for Win95 is enabled
  msg ("Desktop directory for desktop link: %s",path);
  if (strlen (path) == 0) {
     SHGetSpecialFolderLocation (NULL, CSIDL_DESKTOPDIRECTORY, &id);
     SHGetPathFromIDList (id, path);
     msg ("Desktop directory for deskop link changed to: %s",path);
  }
  // end of Win95 addition
  char *fname = concat (path, "/", title, ".lnk", 0);

  if (_access (fname, 0) == 0)
    return 0; /* already exists */

  fname = concat (path, "/", title, ".pif", 0); /* check for a pif as well */

  if (_access (fname, 0) == 0)
    return 0; /* already exists */

  return IDC_ROOT_DESKTOP;
}

static int check_startmenu (char *title, char *target)
{
  char path[_MAX_PATH];
  LPITEMIDLIST id;
  int issystem = (root_scope == IDC_ROOT_SYSTEM) ? 1 : 0;
  SHGetSpecialFolderLocation (NULL, issystem ? CSIDL_COMMON_PROGRAMS : CSIDL_PROGRAMS, &id);
  SHGetPathFromIDList (id, path);
  // following lines added because it appears Win95 does not use common programs
  // unless it comes into play when multiple users for Win95 is enabled
  msg ("Program directory for program link: %s",path);
  if (strlen (path) == 0) {
     SHGetSpecialFolderLocation (NULL, CSIDL_PROGRAMS, &id);
     SHGetPathFromIDList (id, path);
     msg ("Program directory for program link changed to: %s",path);
  }
  // end of Win95 addition
  strcat (path, "/Cygnus Solutions");
  char *fname = concat (path, "/", title, ".lnk", 0);

  if (_access (fname, 0) == 0)
    return 0; /* already exists */

  fname = concat (path, "/", title, ".pif", 0); /* check for a pif as well */

  if (_access (fname, 0) == 0)
    return 0; /* already exists */

  return IDC_ROOT_MENU;
}

static void
save_dialog (HWND h)
{
  root_desktop= rbget (h, da);
  root_menu = rbget (h, ma);
}

static BOOL
dialog_cmd (HWND h, int id, HWND hwndctl, UINT code)
{
  switch (id)
    {

    case IDC_ROOT_DESKTOP:
    case IDC_ROOT_MENU:
      save_dialog (h);
      check_if_enable_next (h);
      break;

    case IDOK:
      save_dialog (h);
      do_desktop_setup();
      NEXT (IDD_S_POSTINSTALL);
      break;

    case IDC_BACK:
      save_dialog (h);
      NEXT (IDD_CHOOSE);
      break;

    case IDCANCEL:
      NEXT (0);
      break;
    }
}

static BOOL CALLBACK
dialog_proc (HWND h, UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message)
    {
    case WM_INITDIALOG:
      load_dialog (h);
      return FALSE;
    case WM_COMMAND:
      return HANDLE_WM_COMMAND (h, wParam, lParam, dialog_cmd);
    }
  return FALSE;
}

void
do_desktop (HINSTANCE h)
{
  CoInitialize (NULL);

  verinfo.dwOSVersionInfoSize = sizeof (verinfo);
  GetVersionEx (&verinfo);

  root_desktop = check_desktop("Cygwin",backslash (concat (root_dir, "/cygwin.bat", 0)));
  root_menu = check_startmenu("Cygwin Bash Shell",backslash (concat (root_dir, "/cygwin.bat", 0)));

  int rv = 0;

  rv = DialogBox (h, MAKEINTRESOURCE (IDD_DESKTOP), 0, dialog_proc);
  if (rv == -1)
    fatal (IDS_DIALOG_FAILED);
}
