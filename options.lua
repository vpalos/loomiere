--
-- Loomiere main configuration options.
--

-- IP (or host-name) of the network interface on which to accept client
-- connections (here '*' means any available interface). By default all
-- available interfaces are bound.
options.bind = '*'

-- TCP/IP port on which to listen for incoming connections. Normally it
-- should be 80 but can be any other port (however, streaming on a port
-- other than 80 may be problematic for fire-walled clients).
options.port = 80

-- The 'root' TCP port is used to accept and serve several very special
-- URLs that are administrative in nature. Note that the 'root' port
-- should be properly firewalled! Currently, only the '/monitor' URL is
-- recognized, producing statistical data formatted to be easily parsed
-- by RRD-like tools (i.e. Munin plugins). To disable set this to nil.
options.root = 81

-- Number of worker threads. Loomiere uses a complex streaming algorithm
-- that makes heavy use of SMP (multi-core) architectures, so having more
-- CPU cores greatly increases streaming capacity. This defaults to the
-- constant 'options.cpus' which is the CPU core count on the system, but
-- you could increase (or multiply) this number to increase performance.
options.workers = 2

-- The maximum number of clients that may be served simultaneously; any
-- excess clients are dropped (i.e. clear-cut anti-dos mechanism). You
-- can set this to 0 or negative to disable the restriction altogether.
-- You should start with a lower value here (e.g. 1000) and increase it
-- gradually stopping when your hardware/bandwidth no longer keeps up.
options.clients = 10000

-- Periods of (full speed) pre-buffering before limiting bandwidth, i.e.
-- the streamer will constantly try to ensure that this many seconds of
-- time are transferred at full speed, ahead of the play-head position
-- in the player. This feature is fully aware of variable bit-rates and
-- it drastically reduces the costs of bandwidth utilization. Setting
-- this to 0 disables throttling (unrecommended). Default is 20 seconds.
options.throttle = 20

-- There are many types of data that are expensive to produce, therefore
-- they are cached straight in memory (RAM) for fast reuse. This is the
-- maximum amount of memory (in MegaBytes) the server is allowed to use
-- for caching purposes. By default this is set to 64 MegaBytes (which is
-- very low). Set it to 0 to disable caching entirely (not recommended).
options.cache = 64

-- Virtual hosts table, where each can be served from a distinct path,
-- each having an URL routing table. Hosts are in fact Lua regexps (read
-- http://www.lua.org/manual/5.1/manual.html#5.4.1) and they are matched
-- against the HTTP request headers in the exact order of definition! Any
-- number of hosts can be defined and the "catch-all" host (i.e. '') must
-- always be last. Note that the escape character is the precent sign '%'
-- (i.e. 'example%.com'). Samples:
--
-- - serve only the 'video.myblog.net' domain:
--   options.hosts['^video%.myblog%.net$'] = { ... }
--
-- - serve the domain 'example.com' and all its sub-domains:
--   options.hosts['(^|%.)example%.com$'] = { ... }
--
-- - serve all the 'video' sub-domains (regardless of domain):
--   options.hosts['^video%.'] = { ... }
options.hosts[''] = {
    -- Entries must first have a field named 'match'
    --  that will
    -- match incoming URLs (in exact order of definition).
    --
    -- The second field must be one of: 'rewrite' (alters the URL in-place),
    -- 'redirect' (send HTTP temporary redirection, code 302) or 'error'
    -- (send the given HTTP error code: 403, 404, 500 etc.). The value (the action field) can be a replacement string
    -- (with references to sub-captures from 'match') or a Lua function
    -- (taking the original URL as argument and returning the target URL).
    -- For 'route' and 'moved' actions (only), the target URL can be either
    -- absolute or relative, (for 'alter' actions only relative URLs can be
    -- used).

    -- Root folder for this virtual host (i.e. where files reside).
    folder = '/var/www',

    -- URL routing table for this virtual host. Samples:
    --
    -- Synopsis:
    --
    -- { match = '«pattern»',
    --   route = '«new URL»' | function(url, host, client_ip, client_port)
    --                             ...
    --                             return «HTTP response code»[, '«new URL»']
    --                         end },
    -- Notes:
    -- - When the route field is a string, then a simple Lua regexp replace
    --   operation is performed on the original URL; the route string can use
    --   references to captures if they exist (i.e. '%1-9').
    --
    -- - When the route field is a function, this function is called with the
    --   shown parameters and is expected to return two values: the HTTP status
    --   code (a number: 200, 301, 302, 404 etc.) and a «new URL» value which
    --   is only needed for codes 200, 301 and 302:
    --
    --   - HTTP OK (in-place rewrite):
    --     return 200, «new URL»
    --
    --   - HTTP permanent redirection:
    --     return 301, «new URL»
    --
    --   - HTTP temporary redirection:
    --     return 302, «new URL»
    --
    --   - Access forbidden:
    --     retun 403
    --
    --   ...
    --
    -- Samples:
    --
    -- - Rewrite an old URL format (in-place):
    --   { match = '^/xmoov%.php%?file=([^&]+).*$',
    --     alter = '/%1?start=%3' },
    --
    -- - Redirect the client to another URL (HTTP 302):
    --   { match = '^/xmoov%.php%?file=([^&]+).*$',
    --     route = 'http://google.com/search?q=%1' },
    --
    -- - Rewrite the URL using a Lua function:
    --   { match = '^/xmoov%.php%?file=([^&]+)',
    --     route = function(url, headers, client_ip, client_port)
    --                 ...
    --                 return 200, '«new URL»'
    --             end }
    --
    -- - Redirect URL permanently to another destination:
    --   { match = '^/xmoov%.php',
    --     moved = function(url, headers, client_ip, client_port)
    --                 ...
    --                 return 301, «new URL»
    --             end },
    routes = {
    }
}

-- These are the mime type mappings which are served by Loomiere. Any
-- other file types are rejected. This table can be used to masquerade
-- file types (e.g. having video files end in '.pdf' instead of '.mp4').
-- Remember that Loomiere never advises clients (e.g. browsers) to cache
-- the served content; it is assumed that the content is always dynamic!
options.mimes = {
    ['video/x-flv'] = 'flv',
    ['video/mp4'] = 'mp4,m4v,m4a,mov,3gp,3g2,f4v,f4a',
    ['image/jpeg'] = 'jpg,jpeg',
    ['image/gif'] = 'gif',
    ['image/png'] = 'png',
    ['image/bmp'] = 'bmp',
    ['image/tiff'] = 'tif,tiff',
    ['application/x-shockwave-flash'] = 'swf'
}
