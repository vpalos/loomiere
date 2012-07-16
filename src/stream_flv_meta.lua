--
-- The Loomiere Project (http://valeriu.palos.ro/loomiere/).
--
-- parse_flv_aux.lua: FLV parsing helpers.
--
-- Read the LICENSE file!
-- Copyright (C)2010 Valeriu Paloş (valeriu@palos.ro). All rights reserved!
--

--------------------------------------------------------------------------------------------------------------

local amf = require'amf'
local core = require'core'
local lpeg = require'lpeg'

local math, table = math, table

module'flv'

--------------------------------------------------------------------------------------------------------------

-- Parse onMetaData tag and produce
-- all neccessary meta-information.
function onMetaData(data, period, start, stop, spatial, file_length)

    -- Parse onMetaData.
    local parse = amf.decode(data, amf.amf0:value())

    -- Check validity.
    if not parse.duration or
       not parse.keyframes.times or
       not parse.keyframes.filepositions then
        return nil
    end

    -- Localize.
    local times = parse.keyframes.times
    local spots = parse.keyframes.filepositions

    -- Sanity checks.
    if #times ~= #spots or #times < 1 or period == 0 then
        return nil
    end

    -- Correct start.
    if times[1] ~= 0 then
        table.insert(times, 1, 0)
        table.insert(spots, 1, spots[1])
    end

    -- Initalize.
    local time = 1
    local here = { time = times[time], spot = spots[time] }
    local next = { time = here.time, spot = here.spot }
    local tend = times[#times]
    local last = {}
    local offsets = {}
    local periods = 1
    offsets[periods] = next.spot

    -- Walk key-points.
    local eof = false
    repeat
        last.time, last.spot = next.time, next.spot
        next.time = next.time + period

        -- Overcome.
        while here.time < next.time do
            last.time, last.spot = here.time, here.spot

            -- Next frame.
            time = time + 1
            if not times[time] then
                eof = true
                break
            end
            here.time, here.spot = times[time], spots[time]

            -- Check order.
            if here.time < last.time then
                return nil
            end
        end

        -- Advance.
        next.spot = last.spot
        if here.time > last.time then
            next.spot = next.spot +
                        ((next.time - last.time) * (here.spot - last.spot)) /
                        (here.time - last.time);
        end

        -- Store.
        if last.time < next.time then
            periods = periods + 1
            offsets[periods] = math.floor(next.spot)
        end

    until next.time >= tend or eof

    -- Endpoint.
    periods = periods + 1
    offsets[periods] = file_length

    -- Data ends.
    local file_offset = spots[1]
    local file_finish = file_length

    -- Choose units.
    local points = times
    if spatial then
        points = spots
    end

    -- Sanity checks.
    if start < points[1] then
        start = 0
    end
    if stop > points[#points] then
        stop = 0
    end

    -- Locate ends.
    if start > 0 then
        for i = #points, 1, -1 do
            if points[i] <= start then
                file_offset = spots[i]
                start = times[i]
                break
            end
        end
    end
    if stop > 0 then
        for i = #points, 1, -1 do
            if points[i] <= stop then
                file_finish = spots[i]
                stop = times[i]
                break
            end
        end
    end

    -- Sanity checks.
    if file_finish < file_offset then
        file_finish = file_offset
        stop = start
    end

    -- Done.
    return offsets, periods, file_offset, file_finish, start, stop
end
