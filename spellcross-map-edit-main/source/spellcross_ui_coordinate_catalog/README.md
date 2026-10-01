# Spellcross – katalog přesných UI souřadnic a grafických zdrojů

Vygenerováno z původní anglické instalace `Spellcros_EN_Verze_Puvodni.zip` a ověřeno proti formátům používaným projektem Spellcross Map Editor / Mod Launcher.

## Co je uvnitř

- **112 přesných obdélníků** z **14 `*.QH` souborů**. Souřadnice jsou `x,y,width,height` v nativním prostoru **640×480 px**, počátek vlevo nahoře.
- Z toho **68 strategických** a **44 taktických** prvků.
- **528 `*.LZ` / `*.LZ0` grafických assetů**: 274 v `COMMON.FS` a 254 v `INFO.FS`. U každého je archivní offset, komprimovaná a přesná dekomprimovaná délka.
- **57 `*.LST` souborů** s celkem **168 odkazy** na doplňkové obrázky jednotek.

## Důležité zjištění o názvech

- V dodané původní hře **není žádný soubor `STREES.QH`**. Existují `STRBAR.QH`, `STRBUY.QH`, `STRHIER.QH`, `STRINFO.QH`, `STRMAP.QH`, `STRMAP1.QH`, `STRMAP2.QH`, `STROPT.QH`, `STRRES.QH`, `STRRSR.QH`, `STRSTAT.QH`, `STRUPG.QH`; všechny jsou zpracované.
- V archivech **není žádný `*.LS`**. Je tam **57 `*.LST`**; ty neobsahují souřadnice, ale názvy dalších `*.LZ` obrázků pro jednotkové info.

## Co přesně znamená LZ

`*.LZ`/`*.LZ0` je komprimovaný grafický datový proud. Samotný formát neobsahuje umístění bitmapy na obrazovce (`x/y`). Proto jsou v `lz_assets_all.csv` pole `placement_x`/`placement_y` úmyslně prázdná a `embedded_screen_position=false`. Tohle je důležité: nechtěl jsem vyrobit falešné „přesné“ souřadnice z pouhého pořadí pixelů.

Některé rozměry bitmap lze bezpečně určit z názvu + přesného počtu rozbalených pixelů (např. `HELP640.LZ0` 640×480, strategické panely `BUY.LZ`/`HIERARCH.LZ` 406×464). Ty jsou uvedeny jako `native_width/native_height`; ostatní zůstávají nevyplněné, dokud není k dispozici externí layout kontext.

## Soubory

- `spellcross_ui_coordinates_master.json` – hlavní, plně strukturovaný dataset.
- `spellcross_ui_coordinates.sqlite` – stejná data pro SQL dotazy; tabulky `qh_rectangles`, `lz_assets`, `lst_links`, `meta`.
- `qh_rectangles_all.csv` – všech 112 souřadnicových obdélníků.
- `qh_rectangles_strategy.csv` / `qh_rectangles_tactical.csv` – rozdělení podle režimu.
- `lz_assets_all.csv` – všech 528 LZ/LZ0 assetů.
- `lst_links_all.csv` – vazby z LST na další unit-info LZ.
- `source_inventory.csv` – kontrolní inventář zdrojových FS archivů včetně SHA-256.
- `raw/qh/` a `raw/lst/` – přesné extrahované originály.
- `overlays/*.svg` – rychlá vizualizace každého QH layoutu na 640×480 plátně.
- `extract_spellcross_layout.py` – malý znovupoužitelný extraktor pro další verze hry.

## Souřadnicová konvence

QH záznam `590,131,37,24` znamená:

- `x=590`, `y=131`
- `width=37`, `height=24`
- rozsah pixelů včetně konce: `590..626 × 131..154`
- pravý/dolní okraj v běžné half-open konvenci: `627,155`

Pro screenshot 1280×960 stačí všechny čtyři hodnoty násobit 2.

## Kontrola úplnosti

- QH: všechny QH položky nalezené ve všech dodaných `*.FS` archivech; pouze `COMMON.FS` obsahuje QH.
- LZ/LZ0: všechny položky ze všech dodaných `*.FS` archivů; LZ grafika je v `COMMON.FS` a `INFO.FS`.
- LST: všechny nalezené LST; pouze `INFO.FS`.
- `STREES.QH`: 0 nálezů.
- `*.LS`: 0 nálezů.
