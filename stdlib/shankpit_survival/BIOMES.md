# SURVIVAL biomes

Founder 2026-11-28: *"a mixture of what a minecraft world would look like and diablo 2"*; meadow and swamp first, then every zone, dungeons next.

- **Diablo 2 rule**: a named GFD zone keeps its biome (`surv-biome-of` in `nk_host_survival.prn` maps the zone name). The layout, tint and tree count still come from the zone seed.
- **Minecraft rule**: an unnamed zone is sent as 255 and `sv-biome-pick` chooses one from the seed.
- Per zone the seed also shifts the floor colour a little and thins/thickens the trees by up to 30%.
- What a biome sets today (`sv_biome.prn`): floor colour, sky + fog colours, tree density, tree kinds (pine/oak/birch), canopy tint. Trees "off" = density near 0.

| id | biome | GFD zones (lore) | floor | trees /1000 cells | kinds |
|---|---|---|---|---|---|
| 0 | city | the original SURVIVAL level | engine | rules' own | all |
| 1 | meadow | Meadow, Fledgling Field | green | 200 | oak, birch |
| 2 | swamp | Swampville, Pellet Marsh, Cormorant Marsh | murky olive, green haze sky | 320 | oak, olive canopy |
| 3 | hills | Hills | pale green | 70 | pine |
| 4 | salt flats | Salt Flats | white-cream, bright sky | 12 | birch, dry canopy |
| 5 | coast | Tidepool Fields, Sunken Chapel Coast, Kuroshio, Handington Docks, Open Balance Quay | sand | 35 | birch, oak |
| 6 | moor | Footnote Moor | heather brown-grey, overcast | 28 | pine, oak |
| 7 | ridge / tor | Observation Tor, Eastwind Ridge, Wren's Perch, Protocol Wall | stone grey | 90 | pine |
| 8 | terraces | Pilgrim's Terraces, Shrine Stair of a Thousand Gates | dry gold-green | 130 | pine, birch |
| 9 | forest | seed only | dark green | 520 | all |
| 10 | snow (taiga) | seed only | white, frosted pines | 240 | pine |
| 11 | town | New Handington | paved grey | 45 | oak |
| 12 | badlands | seed only | red clay, dusty sky | 18 | oak, scrub canopy |
| 13 | hollow | caves, mines, archives, dungeon rooms (stand-in) | dark stone, dark sky | 0 | none |

Not done yet: dungeon pass (13 is a placeholder), biomes that blend across neighbouring rooms (needs room coordinates from GFD), water, rivers, rocks/boulders, per-biome mobs.
