--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- server.lua: High-performance, event-based, generic TCP server.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

local core = require'core'
local ev = require'ev'
local lpeg = require'lpeg'
local monitor = require'monitor'
local rex = require'rex_pcre'
local socket = require'socket'

local P, R, V, S, C, Cs, Cb, Cg, Ct, Cmt =
      lpeg.P, lpeg.R, lpeg.V, lpeg.S, lpeg.C, lpeg.Cs, lpeg.Cb, lpeg.Cg, lpeg.Ct, lpeg.Cmt

local ID, ipairs, os, pairs, setmetatable, table, tonumber, tostring, type =
      ID, ipairs, os, pairs, setmetatable, table, tonumber, tostring, type

module'service'

--------------------------------------------------------------------------------------------------------------

-- Templates.
local templates = {}

-- HTTP headers.
templates.headers = {}

-- Redirect HTTP header.
templates.headers.redirect = [[
HTTP/%s %s
Location: %s

]]

-- Content HTTP headers.
templates.headers.content = {}

-- Dynamic content HTTP header.
templates.headers.content.dynamic = ([[
HTTP/%%s %%s
Content-Type: %%s; charset=UTF-8
Content-Length: %%u
Cache-Control: no-store, no-cache, must-revalidate, post-check=0, pre-check=0
Expires: Mon, 29 Mar 1982 12:00:00 GMT
Server: %s

%%s
]]):format(ID.name)

-- Static content HTTP header.
templates.headers.content.static = ([[
HTTP/%%s %%s
Content-Type: %%s; charset=UTF-8
Content-Length: %%u
Cache-Control: public
Expires: %s
Server: %s

%%s
]]):format(os.date('%c', os.time() + (86400 * 365)), ID.name)

-- HTML bodies.
templates.bodies = {}

-- HTML error body.
templates.bodies.error = ([[
<html>
    <head>
        <title>%%s</title>
        <link rel="shortcut icon" href="javascript:void(0)" />
    </head>
    <body style="font-family: Courier New, Courier, monospace">
        <h1>%%s</h1>
        <p>ADDRESS: <a href=%%q>%%s</a>
        <br />DETAILS: <strong>%%s</strong></p>
        <hr size="1" noshade="noshade" />
        <small>%%s<br />%s (%s), version %s.</small>
    </body>
</html>
]]):format(ID.name, ID.details, ID.version)

--------------------------------------------------------------------------------------------------------------

-- Prototype.
local client = { loop      = ev.Loop.default,
                 timeout   = 5,
                 hosts     = {},
                 call      = function() end,
                 callbacks = {} }

-- Constructor (takes-over socket).
function client:new(instance)

    -- Instance.
    instance = instance or {}
    instance.__index = self
    setmetatable(instance, instance)

    -- Configure.
    instance.socket:settimeout(0)
    instance.ip = instance.socket:getpeername()

    -- Actions.
    instance.actions = { [ev.READ] = {}, [ev.WRITE] = {} }

    -- Watchers.
    for event, action in pairs(instance.actions) do
        action.watchers = {
            ['timeout'] = ev.Timer.new(function() self.callbacks[ev.TIMEOUT](instance, event) end,
                                       instance.timeout, instance.timeout),
            ['io'] = ev.IO.new(function()
                                   action.activity = instance.loop:now()
                                   self.callbacks[event](instance)
                               end,
                               instance.socket:getfd(),
                               event)
        }
    end

    -- Schedule HTTP parsing.
    instance:read('\n\r?\n', instance.parse)

    -- Ready.
    return instance
end

-- Destructor (once created, the client object must be destroyed explicitly
-- by the handling code since there are no finalizers for pure-Lua objects).
function client:destroy(error)

    -- Events.
    if self.loop then
        for i, action in pairs(self.actions) do
            for j, watcher in pairs(action.watchers) do
                watcher:stop(self.loop)
            end
        end
        self.loop = nil
    end

    -- Socket.
    if self.socket then

        self.socket:shutdown()
        self.socket:close()
    end
    self.socket = nil

    -- Tell.
    if error then
        core.warning(('Server-client communication failure: %s'):format(error))
        self:monitor('net', '+failures')
    end

    -- Instance.
    self.callback = nil
    self = nil

    -- Nillify.
    return nil
end

--------------------------------------------------------------------------------------------------------------

-- Monitor.
function client:monitor(...)
    if self.stats then
        monitor(...)
    end
end

-- I/O launcher.
function client:action(event, data, callback)

    -- Assign.
    local action = self.actions[event]

    -- Integrity.
    if not self.loop or action.watchers['timeout']:is_active() then
        return nil
    end

    -- Context.
    if event == ev.READ then
        action.amount = tonumber(data)
        action.pattern = (not action.amount) and tostring(data)
    else
        action.data = tostring(data)
        action.amount = data:len()
    end

    -- Prepare.
    action.callback = callback
    action.activity = self.loop:now()

    -- Launch timer.
    action.watchers['timeout']:start(self.loop)

    -- Input buffer dry-run.
    if (event == ev.READ) then
        self.callbacks[event](self)
    end

    -- If unsatisfied, go live.
    if action.watchers['timeout']:is_active() then
        action.watchers['io']:start(self.loop)
    end

    -- Scheduling success.
    return true
end

-- Timer callback.
client.callbacks[ev.TIMEOUT] = function(self, event)

    -- Assign.
    local action = self.actions[event]

    -- Measure.
    local current = self.loop:now()
    local timeout = action.activity + self.timeout

    -- Verify.
    if timeout < current then
        self:destroy('I/O timeout!')
    else
        action.watchers['timeout']:again(self.loop, timeout - current)
    end
end

-- Read workhorse callback.
client.callbacks[ev.READ] = function(self)

    -- Assign.
    local action = self.actions[ev.READ]

    -- Receive.
    local data, status, residue = nil, nil, nil
    if action.input then
        data = action.input
    else
        data, status, residue = self.socket:receive(action.amount or 262144)
    end
    data = data or residue
    action.input, residue = nil, nil
    action.buffer = action.buffer or {}

    -- Check.
    if status == 'closed' then
        self:destroy('Connection dropped!')
        return
    elseif not data then
        return
    end

    -- Advance.
    local complete = false
    if action.pattern then
        local head = action.buffer[#action.buffer] or ''
        local tail = head..data
        local a, b = tail:find(action.pattern)
        if a then
            residue  = data:sub(b + 1)
            data     = data:sub(1, a - 1)
            complete = true
        end
    elseif action.amount then
        action.amount = action.amount - data:len()
        complete = action.amount == 0
    else
        complete = true
    end

    -- Append.
    action.buffer[#action.buffer + 1], data = data, nil
    action.input = residue ~= '' and residue or nil

    -- Finish.
    if complete then

        -- Assemble.
        local data = table.concat(action.buffer)
        local callback = action.callback
        action.buffer = nil
        action.callback = nil

        -- Stop events.
        action.watchers['timeout']:stop(self.loop)
        action.watchers['io']:stop(self.loop)

        -- Clean-up.
        action.amount = nil
        action.pattern = nil

        -- Invoke callback.
        if callback then
            callback(self, data)
        end
    end
end

-- Write workhorse callback.
client.callbacks[ev.WRITE] = function(self)

    -- Assign.
    local action = self.actions[ev.WRITE]

    -- Send.
    local amount, status, partial = self.socket:send(action.data, action.offset or 1)
    action.offset = amount or partial


    -- Check.
    if status == 'closed' then
        self:destroy('Connection dropped!')
        return
    elseif not action.offset then
        return
    end

    -- Finish.
    if action.offset == action.amount then

        -- Assemble.
        local data = action.data
        local callback = action.callback
        action.data = nil
        action.callback = nil

        -- Stop events.
        action.watchers['timeout']:stop(self.loop)
        action.watchers['io']:stop(self.loop)

        -- Clean-up.
        action.amount = nil
        action.offset = nil

        -- Invoke callback.
        if callback then
            callback(self, action.data)
        end
    end
end

--------------------------------------------------------------------------------------------------------------

-- Asynchronous and non-blocking read operation. The 'limit' argument can be:
--   a) pattern: reads data until this pattern is found;
--   b) number: reads this many bytes of data;
--   c) nil: reads what is available in the socket buffer.
-- Invokes callback(client, data) when reading is done. Returns true if the
-- operation was scheduled successfully or nil if another I/O operation was
-- already running at the time of calling.
function client:read(limit, callback)
    return self:action(ev.READ, limit, callback)
end

-- Asynchronous and non-blocking write operation of the 'data' to the socket.
-- Invokes callback(client, data) when writing is done. Returns true if the
-- operation was scheduled successfully or nil if another I/O operation was
-- already running at the time of calling.
function client:write(data, callback)
    return self:action(ev.WRITE, data, callback)
end

--------------------------------------------------------------------------------------------------------------

-- HTTP generic redirect.
function client:_redirect(code, location)

    -- Assemble.
    local data = templates.headers.redirect:format(self.request.http, code, location)

    -- Log.
    core.info(('%s - HTTP/%s %s - %q -> %q'):format(self.ip,
                                                    self.request.http,
                                                    code,
                                                    self.request.url,
                                                    location))
    -- Monitor.
    self:monitor('net',
                 '+outgoing',
                 '+'..self.request.host,
                 '+'..code:match('^[0-9]+'))

    -- Output.
    self:write(data, client.destroy)
end

-- HTTP generic response.
function client:_content(code, mime, kind, content, length, callback)

    -- Assemble.
    length = length or content:len()
    local data = templates.headers.content[kind or 'dynamic']
    data = data:format(self.request.http, code, mime, length, content)

    -- Log.
    core.info(('%s - HTTP/%s %s - %q <- %q'):format(self.ip,
                                                    self.request.http,
                                                    code,
                                                    self.request.url,
                                                    self.request.referer or ''))

    -- Monitor.
    self:monitor('net',
                 '+outgoing',
                 '+'..self.request.host,
                 '+'..code:match('^[0-9]+'))

    -- Output.
    self:write(data, callback or client.destroy)
end

-- HTTP generic error response.
function client:_error(code, message)
    self:_content(code, 'text/html', 'dynamic', templates.bodies.error:format(code, code,
                                                                              self.request.url,
                                                                              self.request.url,
                                                                              message, os.date()))
end

--------------------------------------------------------------------------------------------------------------

-- HTTP/HTML error responses.
function client:error_404(message) self:_error('404 Not Found', message) end
function client:error_403(message) self:_error('403 Forbidden', message) end
function client:error_500(message) self:_error('500 Internal Server Error', message) end
function client:error_503(message) self:_error('503 Service Unavailable', message) end

-- HTTP redirections.
function client:redirect_301(location) self:_redirect('301 Moved Permanently', location) end
function client:redirect_302(location) self:_redirect('302 Found', location) end

-- HTTP content responses.
for kind in pairs(templates.headers.content) do
    client[kind..'_200'] = function(self, mime, content, length, callback)
                               return client._content(self, '200 OK', mime, kind, content, length, callback)
                           end
end

--------------------------------------------------------------------------------------------------------------

-- URL assembler.
function client:_render_url()
    self.request.url = ('http://%s%s%s'):format(self.request.host,
                                                self.request.port and (':'..self.request.port) or '',
                                                self.request.get)
end

-- HTTP request parser.
function client:parse(headers)

    -- Check if valid headers.
    if not headers then
        return false
    end

    -- Header parser.
    lpeg.locale(lpeg)
    local grammar_a = P{ 'entry';
        entry       = Ct((V'h_get' + V'h_host' + V'h_referer' + 1)^0),

        h_get       = P'GET ' * V'get' * P' HTTP/' * V'http' * V'limit',
        h_host      = P'Host: ' * V'host' * V'port'^-1 * V'limit',
        h_referer   = P'Referer: ' * V'referer' * V'limit',

        get         = Cg((1 - lpeg.space)^1, 'get'),
        http        = Cg((R'09' + P'.')^3, 'http'),
        host        = Cg((1 - (lpeg.space + P':'))^1, 'host'),
        port        = P':' * Cg(R'09'^1, 'port'),
        referer     = Cg((1 - lpeg.space)^1, 'referer'),

        limit       = P'\r'^0 * P'\n'
    }
    self.request = lpeg.match(grammar_a, headers)
    self.request.http = self.request.http or '1.0'
    self.request.host = self.request.host or self.socket:getsockname()

    -- Count incoming.
    self:monitor('net', '+incoming', '+'..self.request.host)

    -- Check success.
    if not self.request.get or not self.request.http then
        return self:destroy('Malformed request.')
    end

    -- Prepare routing.
    local action, target

    -- Virtual hosting.
    for _, host in ipairs(self.hosts) do

        -- Match host.
        if rex.find(self.request.host, host.rex) then

            -- Sandbox.
            self.request.folder = host.folder

            -- URL routing.
            for _, route in ipairs(host.routes) do

                -- Match route.
                if rex.find(self.request.get, route.rex) then

                    -- Prepare route.
                    action = (route.alter and 'alter') or
                             (route.route and 'route') or
                             (route.moved and 'moved')
                    target = type(route[action])

                    -- Reroute URL.
                    if target == 'function' then
                        target = route[action](self.request.get, headers)
                    else
                        target = rex.gsub(self.request.get, route.rex, route[action])
                    end

                    -- Done.
                    break
                end
            end

            -- Done.
            break
        end
    end

    -- Render URL.
    self:_render_url()

    -- Routing.
    if target then
        if action == 'alter' then
            core.info(('%s - REWRITE - %q -> %q'):format(self.ip, self.request.url, target))
            self.request.get = target
            self:_render_url()
        elseif action == 'route' then
            return self:redirect_302(target)
        else
            return self:redirect_301(target)
        end
    end

    -- Handle strangers.
    if not self.request.folder then
        return self:error_404(('Host %q is unknown!'):format(self.request.host))
    end

    -- Query arguments.
    self.request.args = {}
    local grammar_b = P{ 'entry';
        entry = V'path' * V'args'^0,
        path  = C((1 - P'?')^1),
        args  = S'?&' * Cmt(C((lpeg.alnum + S'_-[]')^1) * (P'=' * C((1 - S'& ')^0))^-1,
                            function(s, o, key, value)
                                self.request.args[key] = value or ''
                                return true
                            end)
    }

    -- URL as segments.
    self.request.segments = {}
    for part in self.request.get:gmatch('/([^/]+)') do
        self.request.segments[#self.request.segments + 1] = part
    end

    -- URL as location.
    self.request.location = lpeg.match(grammar_b, self.request.get)

    -- Invoke handler.
    self:call()

    -- Success.
    return true
end

--------------------------------------------------------------------------------------------------------------

-- Prototype.
loop  = ev.Loop.default
bind  = '*'
port  = 80
call  = client.destroy
hosts = {}

-- Constructor.
function new(self, instance)

    -- Instance.
    instance = instance or {}
    self.__index = self
    setmetatable(instance, self)

    -- Open socket.
    local socket, error = socket.bind(instance.bind, instance.port)
    if not socket then
        core.fatal(('Could not bind to %s:%u: %s!')
                   :format(instance.bind, instance.port, error))
    end

    -- Configure.
    instance.socket = socket
    instance.socket:setoption('reuseaddr', true)
    instance.socket:settimeout(0)

    -- Acceptor.
    local cb_accept = function(loop)

        -- Accept connection.
        local socket, error = instance.socket:accept()
        if not socket then
            core.error(('Connection failed: %s'):format(error))
            return
        end

        -- Spawn client object.
        client:new{ socket = socket,
                    loop   = instance.loop,
                    hosts  = instance.hosts,
                    call   = instance.call,
                    stats  = instance.stats }
    end

    -- Launch.
    instance.client_acceptor = ev.IO.new(cb_accept, instance.socket:getfd(), ev.READ)
    instance.client_acceptor:start(instance.loop)

    -- Ready.
    return instance
end

-- Destructor.
function destroy(self)
    self.client_acceptor:stop(self.loop)
    self.socket:close()
end
