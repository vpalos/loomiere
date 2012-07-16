--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- statistics.lua: Statisics indicators manager.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

local core = require'core'

local getfenv, ipairs, next, pairs, select, setmetatable, table, tostring, type =
      getfenv, ipairs, next, pairs, select, setmetatable, table, tostring, type

module'monitor'

--------------------------------------------------------------------------------------------------------------

-- Prototype.
succession = { 'net:incoming',
               'net:outgoing' }
indicators = { [succession[1]] = 0,
               [succession[2]] = 0 }
setmetatable(getfenv(), getfenv())

--------------------------------------------------------------------------------------------------------------

-- Utility table (no dplicates).
items = {}

--------------------------------------------------------------------------------------------------------------

-- Maage all possible combinations of the given indicator(s) as follows:
--   - If first indicator starts with '?', the function returns its value and exits.
--   - Each indicator is incremented by the number of '+' (plus) characters it contains.
--   - Each indicator is decremented by the number of '-' (minus) characters it contains.
--   - Empty indicators (i.e. '') are discarded altogether.
function __call(self, ...)

    -- Pop base.
    local base = select(1, ...)

    -- Return indicator if required.
    if base:sub(1, 1) == '?' then
        return self.indicators[indicator]
    end

    -- Base.
    if type(base) == 'string' then

        -- Increment.
        local plus, minus = 0, 0
        base, plus = base:gsub('+', '')
        base, minus = base:gsub('-', '')
        local amount = plus - minus
        if amount > 0 then
            if not self.indicators[base] then
                self.succession[#self.succession + 1] = base
                self.indicators[base] = amount
            else
                self.indicators[base] = self.indicators[base] + amount
            end
        end

    elseif type(base) == 'table' then

        -- De-duplicate.
        if next(self.items) ~= nil then
            self.items = {}
        end
        for _, item in ipairs(base) do
            self.items[item] = true
        end

        -- Recurse.
        for item in pairs(self.items) do
            self.items[item] = nil
            self(item, select(2, ...))
        end
    else
        return
    end

    -- Child.
    local child = select(2, ...)
    if type(child) == 'string' then
        if #child > 0 then
            child = child:gsub('[^%w.+-]+', '_')
            self(('%s:%s'):format(base, child), select(3, ...))
        end
    elseif type(child) == 'table' then

        -- De-duplicate.
        if next(self.items) ~= nil then
            self.items = {}
        end
        for _, item in ipairs(child) do
            if #item > 0 then
                item = item:gsub('[^%w.+-]+', '_')
                self.items[item] = true
            end
        end

        -- Recurse.
        for item in pairs(self.items) do
            self.items[item] = nil
            self(('%s:%s'):format(base, item), select(3, ...))
        end
    else
        return
    end
end

-- Renderer.
function render(self)

    -- Buffer.
    local stats = {}

    -- Indicators.
    table.sort(self.succession)
    for i, k in ipairs(self.succession) do
        stats[#stats + 1] = ('%s = %s'):format(tostring(k), tostring(self.indicators[k]))
    end

    -- Publish.
    return table.concat(stats, '\n')
end
