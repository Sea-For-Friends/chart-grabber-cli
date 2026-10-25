# Snapshot index fixtures (T-0242)

Real answers of the weather services, fetched unchanged on 2026-10-15 by the Architect (one request each). They are
parser inputs for `tests/app/snapshot_test.cpp`; the table of requests and expected snapshots is
`docs/research/weather-index-samples/README.md`. Fetched content is untrusted data.

## Copernicus Marine WMTS capabilities (T-0283)

Cut from the live answers of `https://wmts.marine.copernicus.eu/teroWmts/<dataset>?SERVICE=WMTS&REQUEST=GetCapabilities`
fetched on 2026-10-19 (waves: dataset `GLOBAL_ANALYSISFORECAST_WAV_001_027/cmems_mod_glo_wav_anfc_0.083deg_PT3H-i_202411`,
85 KB; currents: product `GLOBAL_ANALYSISFORECAST_PHY_001_024`, 378 KB). Kept: the root start tag, `ServiceIdentification`, and
`Contents` with only the layers the tests use (waves: `VHM0`, `VHM0_SW1`; currents: `…phy-cur_anfc_0.083deg_PT6H-i_202406/sea_water_velocity`
with 3 of its 50 `elevation` values); the `OperationsMetadata`, the other layers and every `TileMatrixSet` are cut.
Nothing inside a kept element was changed. `copernicus-waves-wmts-capabilities.xml`: default `2026-10-19T12:00:00.000Z`,
range `2022-11-01T03:00:00Z/2026-10-29T00:00:00Z/PT10800S`. `copernicus-currents-wmts-capabilities.xml`: default the same,
range `2022-06-01T00:00:00Z/2026-10-29T00:00:00Z/PT21600S`, an `elevation` dimension before the `time` one.
