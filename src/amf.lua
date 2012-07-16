--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- amf.lua: Action Message Format protocol encoder/decoder.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

--
-- TODO: Implement AMF version 3 decoder for interpreting AMF hint packets.
-- TODO: Implement AMF version 3 encoder for sending AMF responses to clients.
--

--------------------------------------------------------------------------------------------------------------

local core = require'core'
local lpeg = require'lpeg'

local P, R, V, S =
      lpeg.P, lpeg.R, lpeg.V, lpeg.S

local C, Cc, Cs, Cb, Cf, Cg, Cp, Ct, Cmt =
      lpeg.C, lpeg.Cc, lpeg.Cs, lpeg.Cb, lpeg.Cf, lpeg.Cg, lpeg.Cp(), lpeg.Ct, lpeg.Cmt

local getfenv, math, pcall, setmetatable, table =
      getfenv, math, pcall, setmetatable, table

module'amf'

--------------------------------------------------------------------------------------------------------------

--
-- AMF baseground.
--

-- Namespace.
local amf = getfenv()

-- Pivot stack.
amf.pivots = {}

-- Lookup table.
amf.references = { __mode = 'v' }
setmetatable(amf.references, amf.references)

-- Generic value.
function amf:value()
    return Cmt(self.integer(8),
               function(s, i, marker)
                   local parser = self.types[marker] or amf.unsupported(marker)
                   local subset, offset = lpeg.match(parser * Cp, s, i)
                   return offset, subset
               end)
end

-- Unsupported markers.
function amf.unsupported(marker)
    return P(true) / function()
                         error(('Unsupported marker (0x%02X)!'):format(marker), 0)
                     end
end

-- Integers (unsigned).
function amf.integer(bits)
    return P(bits / 8) / core.bin2integer32
end

-- Doubles.
function amf.double()
    return P(8) / core.bin2double64
end

-- Generic arrays (count * item * item ...).
function amf.array(count, item)
    return Cmt(count,
               function(s, i, count)
                   local subset, offset = {}, i
                   for j = 1, count do
                       subset[j], offset = lpeg.match(item * Cp, s, offset)
                       if not offset then
                           return false
                       end
                   end
                   return offset, subset
               end)
end

-- Store pivot.
function amf.pivot(bits)
    return Cmt(amf.integer(bits), function(s, i, value)
                                      table.insert(amf.pivots, i)
                                      table.insert(amf.pivots, value)
                                      return i, value
                                  end)
end

-- Check pivot.
function amf.check(pattern, name)
    return Cmt(pattern, function(s, i, ...)
                            local length = table.remove(amf.pivots)
                            local pivot = table.remove(amf.pivots)
                            if length ~= 0xffffffff and (pivot + length) ~= i then
                                error(('%s does not have %u bytes, as declared!')
                                      :format(name, length), 0)
                            end
                            return i, ...
                        end)
end

-- Store reference.
function amf.refer(object)
    amf.references[#amf.references + 1] = object
    return object
end

-- Purge references.
function amf.reset()
    amf.pivots = {}
    amf.references = {}
end

--------------------------------------------------------------------------------------------------------------

--
-- AMF0 specifics.
--

-- Namespace.
local amf0 = { __index = amf, types = {} }
setmetatable(amf0, amf0)
amf.amf0 = amf0

-- Strings (count * character * character ...).
function amf0.string(bits)
    return Cmt(amf0.integer(bits),
               function(s, i, count)
                   local subset, offset = lpeg.match(C(P(count)) * Cp, s, i)
                   return offset, subset
               end)
end

-- Anonymous objects.
function amf0.anonymous_object()
    return Cmt(P(true),
               function(s, i)
                   local limit = P'\000\000\009'
                   local field = amf0.string(16)
                   local value = amf0:value()
                   local subset = {}
                   while true do
                       local f, v, o = lpeg.match(((field * value) - limit) * Cp, s, i)
                       if not o then
                           break
                       end
                       i = o
                       subset[f] = v
                   end
                   amf.refer(subset)
                   return i + 3, subset
               end)
end

-- Booleans.
amf0.boolean = amf0.integer(8) / function(value) return value ~= 0 end

-- Date timestamp.
amf0.date = (amf0.double() / math.floor) * P(2)

-- Anonymous objects.
amf0.typed_object = Ct(Cg(amf0.string(16), 'class') * Cg(amf0.anonymous_object(), 'object'))

-- Associative arrays.
amf0.associative_array =  P(4) * amf0.anonymous_object()

-- Strict arrays.
amf0.strict_array = amf0.array(amf0.integer(32), amf0:value()) / amf0.refer

-- Object reference.
amf0.reference = amf0.integer(16) / function(ref) return amf0.references[ref + 1] end

-- AMF0 types.
amf0.types = {

    -- Supported markers.
    [0x00] = amf0.double(),                             -- Double.
    [0x01] = amf0.boolean,                              -- Boolean.
    [0x02] = amf0.string(16),                           -- String (16).
    [0x03] = amf0.anonymous_object(),                   -- Anonymous object.
    [0x05] = P(true) / '(nil)',                         -- Null (empty string).
    [0x06] = P(true) / '(undefined)',                   -- Undefined (empty string).
    [0x07] = amf0.reference,                            -- Referenced object.
    [0x08] = amf0.associative_array,                    -- Associative, variable-sized array (ECMA).
    [0x0a] = amf0.strict_array,                         -- Indexed, fixed-sized array (STRICT).
    [0x0b] = amf0.date,                                 -- Date (timestamp).
    [0x0c] = amf0.string(32),                           -- Long string.
    [0x0f] = amf0.string(32),                           -- XML (long) string.
    [0x10] = amf0.typed_object,                         -- Typed object.

    -- Not yet implemented markers.
    [0x11] = amf0.unsupported(0x11),                    -- AMF3 upgrade marker (TODO!).

    -- Unsupported markers.
    [0x04] = amf0.unsupported(0x04),                    -- MovieClip marker.
    [0x09] = amf0.unsupported(0x09),                    -- Object end marker (illegal here).
    [0x0d] = amf0.unsupported(0x0D),                    -- Unsupported marker.
    [0x0e] = amf0.unsupported(0x0E)                     -- RecordSet marker.
}

--------------------------------------------------------------------------------------------------------------

-- AMF3 namespace.
local amf3 = { __index = amf, types = {} }
setmetatable(amf3, amf3)
amf.amf3 = amf3

-- AMF3 types.
amf3.types = {
    -- TODO!
}

--------------------------------------------------------------------------------------------------------------

-- AMF Header.
amf.header = Ct(Cg(amf0.string(16), 'name') *
                Cg(amf0.boolean, 'must-understand') *
                Cg(amf.pivot(32), 'length') *
                Cg(amf.check(amf0:value(), 'AMF header'), 'value'))

-- AMF Message.
amf.message = Ct(Cg(amf0.string(16), 'target-uri') *
                 Cg(amf0.string(16), 'response-uri') *
                 Cg(amf.pivot(32), 'length') *
                 Cg(amf.check(amf0:value(), 'AMF message'), 'value'))

-- Full AMF packet.
amf.packet = Ct(Cg(amf0.integer(16), 'version') *
                Cg(amf0.array(amf0.integer(16), amf0.header), 'headers') *
                Cg(amf0.array(amf0.integer(16), amf0.message), 'messages'))

--------------------------------------------------------------------------------------------------------------

-- Parse an AMF packet into a Lua object.
function decode(packet, parser)
    amf.reset()
    local code, result = pcall(lpeg.match, parser or amf.packet, packet)
    if not code or not result then
        return false, result or 'Invalid AMF syntax!'
    end
    return result
end
