/*
 * The Loomiere Project (http://valeriu.palos.ro/loomiere/).
 *
 * loomiere.c: Server entry point.
 *
 * Read the LICENSE file!
 * Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
 */

#include <lua.h>
#include <lauxlib.h>
#include <sys/resource.h>
#include <stdio.h>

#include "core.h"
#include "engine.h"
#include "favicon.h"
#include "loomiere.h"
#include "server.h"
#include "options.h"
#include "service.h"
#include "monitor.h"

/*
 * The main() function is essentially a wrapper for the internal Lua state
 * machine that does all the server-related work. The C code will register
 * its parsing functionality to be used at will by all the Lua components!
 */
int main(int argc, char* argv[]) {

    // identify
    printf("%s (%s) version %s"
           #ifdef DEBUG
           " (debug version)"
           #endif
           ".\n%s\n", ID_NAME, ID_DETAILS, ID_VERSION, ID_COPYRIGHT);

    // raise descriptor limit
    struct rlimit limit = { 65535, 65535 };
    if (setrlimit(RLIMIT_NOFILE, &limit)) {
        WARNING("Could not set open file descriptor limit, using default!");
    }

    // open main Lua state
    lua_State* L = lua_open();
    luaL_openlibs(L);

    // assemble identity
    static const char* ID[][2] = {
        { "name", ID_NAME },
        { "version", ID_VERSION },
        { "details", ID_DETAILS },
        { "copyright", ID_COPYRIGHT },
        { NULL, NULL }
    };

    // push identity
    lua_newtable(L);
    int index = 0;
    while (ID[index][0]) {
        lua_pushstring(L, ID[index][0]);
        lua_pushstring(L, ID[index][1]);
        lua_settable(L, -3);
        index++;
    }

    // push favicon
    lua_pushstring(L, "favicon");
    lua_pushlstring(L, bin_favicon_ico, sizeof(bin_favicon_ico));
    lua_settable(L, -3);

    // ready
    lua_setglobal(L, "ID");

    // deliver arguments
    lua_checkstack(L, argc + 1);
    lua_createtable(L, 0, argc);
    for (index = 0; index < argc; index++) {
        lua_pushnumber(L, index);
        lua_pushstring(L, argv[index]);
        lua_settable(L, -3);
    }
    lua_setglobal(L, "arg");

    // load C libraries
    load_core_c(L);
    load_engine_c(L);

    // load Lua libraries
    load_monitor_lua(L);
    load_options_lua(L);
    load_service_lua(L);

    // invoke server
    load_server_lua(L);

    // clean-up
    lua_close(L);
    return 0;
}
