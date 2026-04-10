# Spellcross Reloaded

Fanouškovský pokus **oživit a zpřístupnit starý Spellcross** modernějším způsobem. Projekt původně vyšel z map editoru, ale postupně se posouvá hlavně směrem k hratelné hře.

- **Map editor (wxWidgets/C++)** rozšířený o *game mode*
- **Python utility** pro extrakci, dekompresi a rekonstrukci herních dat / UI obrazovek
- průběžná rekonstrukce kampaně, strategických obrazovek a herních mechanik

> Stav projektu: **WIP / experiment / první veřejný release**. Hratelné, ale bez jakékoli záruky.

---

## Release

K dispozici je veřejný release:

[**`Spellcross_Reloaded_v0.0.4`**](https://github.com/luboshorak/spellcross_restoration_tools/releases/tag/v0.0.4)

Aktuální cíl tohoto buildu je prostý: dostat hru do stavu, kdy by měla být **dohratelná od začátku do konce kampaně**. To ale neznamená, že je hotová nebo stabilní.

- testováno na **Windows 11**
- bez jakékoli záruky
- očekává se **velké množství bugů, rozbitých okrajových stavů a placeholder hodnot**

Tenhle release je určen hlavně pro průběžné testování, hledání chyb a ověřování, že se celý projekt konečně posouvá z „editor experimentu“ směrem ke skutečné hře.

---

## Známé nedodělky a problémy v release 0.0.4

### Herní logika
- **Hierarchické začlenění jednotek** se zatím správně nepropisuje do hrací mapy.
- **Upgrady jednotek** se zatím také nepropisují korektně do samotné hry / bojové mapy.
- **Dočasné jednotky** nejsou hotové a jejich systémy aktuálně nefungují správně.
- **Other Side counterattack / protiútoky** nejsou plně funkční.
- **Deployment mod** není implementován - jednotky jsou na začátku mise deploynuty automaticky na startovací pozice

### Game mode / stabilita
- **Game Mode ON** je v této verzi již nastaven automaticky - vypnout se dá z hlavního menu pomocí konzole (~) a příkazem GAMEMODEOFF
- Může zlobit **přehrávání videí**.
- AI nepřátel i aliančních jednotek je zatím spíš **demo verze AI** než hotová herní inteligence.
- Kvůli tomu je momentálně **rozbitá obtížnost hry**.

### Balancing a ekonomika
- **Balanc peněz a výzkumu** není doladěný.
- **Ceny za jednotky, upgrady, akce a další hodnoty** jsou na mnoha místech jen placeholdery.

### UI / vizuál
- **Strategická mapa** zatím používá jen základní grafiku a ne finální grafiku ve stylu původní hry.

---

## Co je tohle za projekt?

Původní myšlenka byla **oživit game mode v editoru**. To se podařilo a od té chvíle se projekt začal posouvat dál – od pouhého editoru směrem k rekonstrukci původního Spellcrossu.

Dnes má projekt dva hlavní směry:

1. **udržet a rozšířit hratelný game mode**
2. postupně vytvořit něco jako vlastní **OpenSpellcross experiment**

To v praxi znamená hlavně:
- načítání map a původních datových formátů
- základní herní mechaniky
- rekonstrukci obrazovek mimo mapu (Strategic level, Hierarchy, Research, Units…)
- kampaňovou logiku a progression
- postupné nahrazování debug/editor workflow skutečným hraním

Právě kvůli rekonstrukci UI a asset pipeline v repu vzniklo i velké množství přiložených Python utilit.

---

## Důležitá poznámka k map editoru

S ohledem na povahu tohoto projektu **není cílem dál výrazně rozvíjet map editor jako samostatný produkt**. To je spíš role původního editoru.

Tenhle projekt se postupně soustředí hlavně na:
- debugging game mode
- stabilizaci kampaně
- doplňování mechanik
- vylepšování samotné hry

Map editor tedy v tomhle repu bude postupně spíš **upozaděn ve prospěch hry samotné**.

---

## Co je aktuálně použitelné

### Game mode na mapě
- možnost přidat jednotky na načtenou mapu a mapu „hrát“
- možnost hrát původní mise
- **save/load stavu** rozehrané hry
- základní AI pro nepřátelské jednotky
- základní přechod mezi taktickou a strategickou částí hry

### Strategická část hry
- rozpracované obrazovky jako:
  - `StrategicLevelFrame`
  - `HierarchyCanvas`
  - `Research`
  - `Units`
- základní campaign flow
- práce se zdroji, výzkumem a jednotkami v nějaké funkční podobě

### Další věci
- skupinový pohyb hráčských jednotek
- main menu / původní menu
- průběžná rekonstrukce dalších částí původního UI

> Prakticky: použitelné to je, ale stále je potřeba počítat s tím, že jde o rozpracovaný build a ne hotovou hru.

---

## Co je dál v plánu

- stabilizace game modu
- opravy campaign flow a okrajových stavů
- dotažení hierarchy / units / research / strategy map obrazovek
- správné propsání upgradů a hierarchie do boje
- dodělání dočasných jednotek
- lepší AI nepřátel i aliančních jednotek
- rozumnější balancing peněz, výzkumu a cen
- postupné nahrazování placeholder grafiky a UI věrnější verzí původní hry

---

## Struktura repa (orientačně)

- `spellcross-map-edit-main/` – C++/wxWidgets editor + game mode, hlavní aplikace
- `spell_extract_fs_gui/` – GUI pro rozbalení `.FS` archivů
- `spell_decomp/` – nástroje pro dekompresi (`LZ`, `LZ0`, `DELZ`)
- `bin_inspector/` – rychlá inspekce a extrakce obsahu z binů
- `spellcross_level_tool_v5/` – skládání a rekonstrukce map/levelů
- `bin_out/spell_ui_builder/` – skládání UI obrazovek z vytažených podkladů
- různé `*_gui.py` a helpery – experimenty a dílčí pipeline kroky

Názvy a umístění se mohou měnit. Repo je živé a část obsahu je stále spíš workbench než finální struktura.

---

## Build (Windows / Visual Studio)

### Požadavky
- **Visual Studio 2022/2026**
- **wxWidgets** buildnuté pro odpovídající MSVC toolchain

### Poznámky
Pokud to chcete zkoušet rozchodit, všechno podstatné je v:

`spellcross-map-edit-main/`

To je hlavní aplikace. Zbytek repa jsou z velké části pomocné utility, experimenty a pipeline skripty.

Pokud narazíte na linker chyby typu **LNK2005 / LNK1169** (duplicitní symboly), zkontrolujte, že implementace UI tříd není omylem ve více `.cpp` souborech zároveň.

---

## Pipeline: jak z toho dostat data a výsledky

Typický postup:

1. **Rozbalit FS soubory**  
   `spell_extract_fs_gui`
2. **Roztřídit data pro orientaci**  
   `data_sorter.py`
3. **Extrahovat LS a LS0 soubory → vzniknou biny**  
   `spell_decomp/spell_bulk_delz_gui.py`
4. **Zjistit, co je uvnitř binů a případně extrahovat**  
   `bin_inspector`
5. **Zpětně komponovat mapy z levelů**  
   `spellcross_level_tool_v5`
6. **Rekonstruovat herní menu/UI z vyextrahovaných podkladů**  
   `bin_out/spell_ui_builder`

---

## Utility na rekonstrukci grafiky Spellcrossu

### `unlz_gui.py`
- rozbaluje **LZ** a **LZ0** soubory do `*.bin`
- výstup je binárka, kterou je potřeba dál interpretovat podle typu dat

### `spell_rawimg_guy_v2.py`
- funkční řešení pro „klasické“ raw obrázky
- vstup: rozbalený `*.bin`
- je potřeba vybrat správnou paletu
- výstup: `*.png`

### `spell_rawimg_gui_v4.py` + `spell_rawimg_tool_v4.py`
- experimentální řešení pro skládanou raw grafiku
- vstup: rozbalený `*.bin`
- výstup: `*.png`
- výsledek je často potřeba ručně doladit:
  - otáčení
  - prokládání (`interleave`)
  - další parametry

### Ostatní soubory
Zbytek jsou různé experimenty, pomocné skripty a pracovní poznámky. Něco funguje dobře, něco částečně a něco vůbec.

---

## Poděkování / Credits

Obrovské díky patří **Stanislavu Mašláňovi** – bez něj by nebylo nic.  
Veškeré unwrapery a původní map editor jsou jeho práce a muselo to stát velké množství času.

Jeho utility:
- https://spellcross.kvalitne.cz/

Originální map editor:
- https://github.com/smaslan/spellcross-map-edit

---

## Jak přispět

- issue / popis problému: ideálně přiložit vzorek souboru, save, screenshot nebo přesný postup reprodukce
- PR vítané, hlavně pokud jde o stabilizaci, cleanup, dokumentaci nebo technické opravy

---

## Licence

Tento projekt je licencován pod **MIT licencí** – viz soubor [`LICENSE`](LICENSE).
