/*
 * This file is part of sp-rtrace package.
 *
 * Copyright (C) 2011 by Nokia Corporation
 *
 * Contact: Eero Tamminen <eero.tamminen@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/**
 * @file gobject_test.c
 *
 * Test application for GObject tracking module (gobject) coverage.
 * 
 * gcc -o gobject_test -O -Wall -rdynamic gobject_test.c \
 *   $(pkg-config --cflags --libs gobject-2.0)
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <glib-object.h>

typedef struct {
  GObject parent ;
} CustomObject ;

typedef struct {
  GObjectClass parent ;
} CustomObjectClass ;


GType custom_object_get_type (void);

#define CUSTOM_TYPE_OBJECT ( custom_object_get_type())


#define CUSTOM_OBJECT ( object ) \
         ( G_TYPE_CHECK_INSTANCE_CAST (( object ), \
           CUSTOM_TYPE_OBJECT , CustomObject ))

G_DEFINE_TYPE ( CustomObject , custom_object , G_TYPE_OBJECT )

static void custom_object_init (CustomObject * obj)
{
}

static void custom_object_class_init(CustomObjectClass * klass)
{
}


int
main (int argc, char *argv[])
{
	g_type_init();

	CustomObject *obj = g_object_newv(CUSTOM_TYPE_OBJECT ,0, NULL);

	g_object_ref(obj);
	g_object_unref(obj);
	g_object_unref(obj);

	sleep(1);

	return 0;
}

