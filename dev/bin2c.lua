#!/usr/bin/env lua
--
-- bin2c.lua: Embedder of binary data into C programs.
--
-- Copyright (C)2010 Valeriu Paloş. All rights reserved!
--
-- This program is an adapted version of the bin2c.lua script written
-- by Mark Edgar. It is licensed the same as Lua, i.e. MIT license.
--
-- Dependencies: lua(5.1), and library: 'bit'(bitlib/bitop)
--

-- Requirements.
local bit = require'bit'

-- Manual.
local description = [[
Usage: lua bin2c.lua filename

Options:
  -h shows a brief informative text on this utility

Inserts a binary file into a C header file which can be included into
an existing C program via the '#include' directive. This file defines
a static variable named 'bin_<name>', where <name> is, basically, the
lowercase name of the given file (without directory) with all the non
alphanumeric characters replaced by underscores. The resulting source
code is as follows:

    static unsigned char bin_<name>[...] = {
        /* ... */
    };

Copyright (C)2010 Valeriu Paloş. All rights reserved.
Licensed under the same terms as Lua (i.e. MIT license).
]]

-- Arguments.
local source
local do_help
for i, v in ipairs(arg or {}) do
    if v:find('^-.+$') then
        do_help     = v:find('h', 1, true)
    else
        source = arg[i]
    end
end
do_help = do_help or not source

-- Help.
if do_help then
    io.stderr:write(description)
    return
end

-- Read file.
local identity = source:lower():gsub('^.+/', ''):gsub('[^%w_]', '_')
local file     = assert(io.open(source, 'rb'))
local content  = file:read'*a'
local length   = content:len()
--file:close()

-- Assemble bytes.
local dump do
    local numtab = {}
    for i = 0, 255 do
        numtab[string.char(i)]=('%3d,'):format(i)
    end
    function dump(string)
        return (string:gsub(".", numtab):gsub(("."):rep(72), '%0\n    '))
    end
end
content = dump(content)

-- Assemble main code.
local output = string.format([[
/*
 * BINARY FILE %q EMBEDDED AS C STATIC DATA.
 */

#ifndef __bin2c_%s__
#define __bin2c_%s__

/*
 * Binary data from file %q.
 */
static unsigned char bin_%s[%u] = {
    %s
};

#endif



]], source, identity, identity, source, identity, length, content)

-- Submit.
io.stdout:write(output)
io.stdout:flush()
