# Loomiere (2010–2011)
 
**A high-performance HTTP audio/video pseudo-streaming server. This is a
finished chapter — preserved for posterity, not maintained. It is meant to be
read, not deployed.**
 
## What it was
 
In 2010, video on the web meant Flash players doing *progressive download*:
the server sent the file over plain HTTP, the player bought seeking with a
`?start=` offset, and bandwidth was the dominant cost of running any video
site. "Pseudo-streaming" was the craft of making that primitive arrangement
smooth, seekable, and affordable.
 
Loomiere was built for that job, and did it: it served **all the video content
of peteava.ro** — a Romanian video-sharing site, now defunct — moving
**about 800 TB per month from 14 servers**, in production for over five years.
A single SSD-based machine (Core i7, 12 GB RAM, 3×1GbE) sustained **6500+
concurrent streams at ~2.8 Gbps** with its disks under 20% load.
 
The architecture: a C core on
[libev](http://software.schmorp.de/pkg/libev.html) with SMP worker threads, an
in-memory cache, and **Lua 5.1 as the configuration and scripting surface** —
virtual hosts, URL routing, even per-request routing logic are Lua, so
integrating with an existing site was an afternoon's work. Statistics are
published on a separate admin port, formatted for Munin/RRD. The design goal,
stated plainly in the old README: *eliminate software bottlenecks entirely, so
the hardware is the only limit.*
 
## The interesting part
 
The piece worth reading is the **VBR-aware bandwidth-conservation throttle**.
The idea: transfer at full speed only until the client holds a configurable
number of seconds of playback *ahead of the play-head* (default 20), then pace
delivery to the stream's own bitrate — tracked through variable-bitrate
material, so the buffer target holds in *seconds of playback*, not bytes.
Smooth playback, no rebuffering on seek, and no bandwidth wasted on video a
viewer abandons. On its first day in production it cut streaming bandwidth by
roughly a third while serving four times more clients than its predecessor —
at 2010 bandwidth prices, that was the entire economics of a video site.
 
The concurrency model deserves its own mention: an event loop per SMP worker
thread, each stream living as a **Lua coroutine** — a blocking operation
yields the coroutine to the C side and resumes when the event loop says so —
with **spinlocks** at the few seams where threads must touch. Thousands of
streams juggled in parallel per machine, designed in 2010, when Node.js was a
year-old curiosity and the async-server style it would popularize was still
exotic.
 
## Why it ended
 
The category was absorbed. nginx's `mp4`/`flv` modules turned pseudo-streaming
into a config flag; CDNs commoditized delivery; and adaptive streaming
(HLS/DASH) replaced the single-file-plus-throttle model entirely. Loomiere's
last release is from December 2011 — roughly when that transition became
irreversible. There is no bitterness in this: platforms absorb categories, and
a product built on a protocol era ends with the era.
 
## Why it is preserved
 
Because the code is the part I'm still proud of: roughly two months of careful
design, then about two weeks of writing. The full story — the era, the
numbers, the graphs, and the lessons — is in the post-mortem:
**[Loomiere: a post-mortem of my favorite code](https://vpalos.com/2026/07/07/loomiere-a-post-mortem-of-my-favorite-code/)**.
 
Start with `options.lua` (the whole design philosophy is legible in its
comments), then the C core under `src/`.
 
## License
 
GPL-3.0 — see [LICENSE](LICENSE) — with one heterodox amendment I stand by:
this software must never be used to stream pornographic content.
© 2010 Valeriu Paloș.
