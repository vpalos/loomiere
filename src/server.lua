--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- server.lua: Frontend server loop.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

local core = require'core'
local ev = require'ev'
local engine = require'engine'
local options = require'options'
local monitor = require'monitor'
local server = require'service'

--------------------------------------------------------------------------------------------------------------

local server = ev.Loop.default
local start = os.time()

--------------------------------------------------------------------------------------------------------------

-- Interrupts.
function cb_signal(loop, watcher, signal)
    watcher:stop(loop)
    loop:unloop()
    core.trace(('Caught %s...'):format(signal))
end

-- Captures.
ev.Signal.new(function(l, w) cb_signal(l, w, 'SIGINT') end, SIGINT):start(server)
ev.Signal.new(function(l, w) cb_signal(l, w, 'SIGTERM') end, SIGTERM):start(server)
ev.Signal.new(function(l, w) cb_signal(l, w, 'SIGHUP') end, SIGHUP):start(server)

--------------------------------------------------------------------------------------------------------------

-- Return a well-formatted time interval since program start.
function elapsed(seconds)
    local seconds = os.difftime(seconds or os.time(), start)
    local days, hours, minutes
    days, seconds = math.modf(seconds / 86400)
    hours, seconds = math.modf(seconds * 24)
    minutes, seconds = math.modf(seconds * 60)
    return ('%ud:%uh:%um:%us'):format(days, hours, minutes, math.floor(seconds * 60 + 0.5))
end

--------------------------------------------------------------------------------------------------------------

-- Workers.
local engine = engine:new{ workers = options.workers,
                           throttle = options.throttle,
                           clients = options.clients,
                           cache = options.cache * 1048576 }

-- Services.
local services = {}

--------------------------------------------------------------------------------------------------------------

-- Serve favicon.
function favicon(client)
    client:static_200('image/x-icon', ID.favicon)
end

--------------------------------------------------------------------------------------------------------------

-- Streaming service.
services['stream'] = service:new{ bind  = options.bind,
                                  port  = options.port,
                                  hosts = options.hosts,
                                  loop  = server,
                                  stats = true }

-- Stream tasks.
local stream = {
    ['/favicon.ico'] = favicon
}

-- Stream service handler.
services['stream'].load = 0
services['stream'].call = function(client)

    -- Specifics.
    if stream[client.request.location] then
        stream[client.request.location](client)
        return
    end

    -- Path and mime type.
    local mime = nil
    local path = core.realpath(client.request.folder..client.request.location)

    -- Check valid path.
    if path then
        local type = path:match('%.([%w_]+)$')
        if type then
            type = (',%s,'):format(type:lower())
            for m, t in pairs(options.mimes) do
                if t:find(type, 1, true) then
                    mime = m:lower()
                end
            end
        end
    end

    -- Check proper file.
    if not path or not mime or not type then
        client:error_404('Unlocatable resource!')
        return
    end

    -- Check security.
    if path:find(client.request.folder, 1, true) ~= 1 or
       not core.readable(path) then
        client:error_403('Access to requested resource is denied!')
        return
    end

    -- Arguments.
    local start = client.request.args['start'] or 0.0
    local stop  = client.request.args['stop'] or 0.0
    local units = (client.request.args['units'] or '?'):sub(1, 1)

    -- Calibrate.
    if units ~= 'b' and units ~= 's' then
        units = 's'
        local integer, fraction = math.modf(math.max(start, stop))
        if fraction == 0 and integer > 3600 then
            units = 'b'
        end
    end

    -- Dispatch.
    local success, error = engine:dispatch{ client  = client,
                                            path    = path,
                                            mime    = mime,
                                            spatial = units == 'b',
                                            start   = start,
                                            stop    = stop }

    -- Success.
    if success then
        core.info(('%s - HTTP/%s %s - %q <- %q'):format(client.ip,
                                                        client.request.http,
                                                        '200 OK',
                                                        client.request.url,
                                                        client.request.referer or ''))
        monitor('net', '+outgoing', '+'..client.request.host, '+200')
        client:destroy()
        client = nil

    -- Server overload.
    elseif error == 'overload' then
        client:error_503('Overload! Please retry in a few minutes!')
        monitor('net', '+overload')

    -- Invalid stream.
    else
        client:error_500(('Stream %q is invalid!'):format(client.request.location))
    end
end

--------------------------------------------------------------------------------------------------------------

-- Administration service.
services['root'] = options.root and
                   service:new{ bind  = options.bind,
                                port  = options.root,
                                hosts = options.hosts,
                                loop  = server }

-- Administration tasks.
local root = {
    ['/favicon.ico'] = favicon,
    ['/monitor'] = function(client)

        -- Assemble.
        local time = os.time()
        local stats = { '# Identification:',
                        ('server = %s'):format(ID.name),
                        ('version = %s'):format(ID.version),
                        ('details = %s'):format(ID.details),
                        ('copyright = %s'):format(ID.copyright),
                        '',
                        '# Configuration:',
                        ('workers = %s'):format(options.workers),
                        ('throttle = %s'):format(options.throttle),
                        '',
                        '# Run-time:',
                        ('start = %s'):format(os.date('%Y-%m-%d %H:%M:%S %Z', start)),
                        ('date = %s'):format(os.date('%Y-%m-%d %H:%M:%S %Z', time)),
                        ('uptime = %s'):format(elapsed(time)),
                        '',
                        '# Cache:',
                        ('cache:limit = %.1f MB'):format(options.cache),
                        ('cache:used = %.1f MB'):format(engine:monitor('cache:used') / 1048576.0),
                        ('cache:items = %u'):format(engine:monitor('cache:items')),
                        ('cache:hits = %u'):format(engine:monitor('cache:hits')),
                        ('cache:misses = %u'):format(engine:monitor('cache:misses')),
                        '',
                        '# Networking:',
                        monitor:render(),
                        '',
                        '# Streaming:',
                        ('clients:limit = %s'):format(options.clients),
                        ('clients:active = %u'):format(engine:monitor('load')),
                        ('data:total = %.1f MB'):format(engine:monitor('data:total') / 1048576.0),
                        ('data:delay = %.3f seconds'):format(engine:monitor('data:delay')) }

        -- Publish.
        client:dynamic_200('text/plain', table.concat(stats, '\n'))
    end
}

-- Administration service handler.
services['root'].call = function(client)
    if root[client.request.location] then
        root[client.request.location](client)
    else
        client:error_403('Unknown request!')
    end
end

--------------------------------------------------------------------------------------------------------------

-- Announce.
core.info(('Server up (%u workers).'):format(options.workers))

-- Serve.
server:loop()

-- Finish.
engine:destroy()
services['stream']:destroy()
services['root']:destroy()

-- Announce.
core.info(('Server down (up %s).'):format(elapsed()))
