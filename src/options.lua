--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- options.lua: Parser of command-line arguments and configuration options.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

local core = require'core'
local getopts = require'alt_getopt'
local rex = require'rex_pcre'

local arg, io, ipairs, loadfile, os, pcall, pairs, print, rawset, setmetatable, tonumber =
      arg, io, ipairs, loadfile, os, pcall, pairs, print, rawset, setmetatable, tonumber

module'options'

--------------------------------------------------------------------------------------------------------------

-- Sorted indexer.
function __sortedindex(self, key, value)
    value['match'] = key
    rawset(self, #self + 1, value)
end

-- Prototype.
bind = '*'
port = 80
workers = 2
clients = 1000
throttle = 20
cache = 256
hosts = setmetatable({}, { __newindex = __sortedindex })
mimes = {}

--------------------------------------------------------------------------------------------------------------

-- Arguments.
local defs = {
    ['help']    = 'h',
    ['options'] = 'o'
}

-- Configure.
local exe = arg[0]
local arg = getopts.get_opts(arg, 'ho:', defs)
if arg['h'] then
    print(('Usage: %s [-h|--help] [-o|--options <.../options.lua>]'):format(exe))
    os.exit()
end

-- Hints.
core.hint("To specify another configuration file, use '--options'.")
core.hint('To stop the server, press [Ctrl+C].')

-- Import file.
file = arg['o'] or '/etc/loomiere/options.lua'
core.info(("Loading configuration from %q."):format(file))
if file then
    local code, error = loadfile(file)
    if not code then 
        core.fatal(error) 
    end
    code, error = pcall(code)
    if not code then 
        core.fatal(error) 
    end
end

-- Compile hosts.
for _, host in ipairs(hosts) do

    -- Sandbox.
    host.folder = core.realpath(host.folder) or
                  core.fatal(('Unresolvable sandbox %q for virtual host %q!')
                             :format(host.folder, host.match))

    -- Qualify sandbox.
    if host.folder:sub(-1, -1) ~= '/' then
        host.folder = host.folder..'/'
    end

    -- Precompile PCRE patterns.
    host.rex = rex.new(host.match)
    for _, route in ipairs(host.routes) do
        route.rex = rex.new(route.match)
    end
end

-- Prepare mimes.
for mime, types in pairs(mimes) do
    mimes[mime] = (',%s,'):format(types:gsub('%s+', ','))
end
