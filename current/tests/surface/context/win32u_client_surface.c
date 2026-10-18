/*
 * Window related functions
 *
 * Copyright 1993, 1994 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

void *client_surface_create( UINT size, const struct client_surface_funcs *funcs, HWND hwnd )
{
    struct client_surface *surface;

    if (!(surface = calloc( 1, size ))) return NULL;
    surface->funcs = funcs;
    surface->ref = 1;
    surface->hwnd = hwnd;

    pthread_mutex_lock( &surfaces_lock );
    list_add_tail( &client_surfaces, &surface->entry );
    pthread_mutex_unlock( &surfaces_lock );

    TRACE( "created %s\n", debugstr_client_surface( surface ) );
    return surface;
}

void client_surface_release( struct client_surface *surface )
{
    ULONG ref = InterlockedDecrement( &surface->ref );
    TRACE( "%s decreasing refcount to %u\n", debugstr_client_surface( surface ), ref );

    if (!ref)
    {
        pthread_mutex_lock( &surfaces_lock );
        if (surface->hwnd)
        {
            surface->funcs->detach( surface );
            list_remove( &surface->entry );
        }
        pthread_mutex_unlock( &surfaces_lock );

        surface->funcs->destroy( surface );
        free( surface );
    }
}
