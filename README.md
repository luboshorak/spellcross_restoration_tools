# Spellcross – restoration tools + úpravy map editoru (OpenSpellcross experiment)

Tohle repo je fanouškovský pokus **oživit a zpřístupnit starý Spellcross** modernějším způsobem.

- **Map editor (wxWidgets/C++)** postupně rozšířený o *game mode*.
- **Python utility** pro extrakci, dekompresi a rekonstrukci herních dat / UI obrazovek.

> Stav: **WIP / experiment**. Některé části jsou použitelné, jiné jsou “research tooling” a mohou být rozbité nebo nedodělané.

---

## Poděkování / Credits

Obrovské díky patří **Stanislavu Mašláňovi** – bez něj by nebylo nic.  
Veškeré unwrapery a původní map editor jsou jeho práce a muselo to stát velké množství času.

Jeho utility (ke stažení):  
- https://spellcross.kvalitne.cz/

Originální map editor:  
- https://github.com/smaslan/spellcross-map-edit

---

## Co je tohle za projekt?

Původní myšlenka byla **oživit game mod v editoru** – to se podařilo a dál se to rozšiřuje o prvky, které v původním editoru nejsou.

Druhý směr je něco jako **OpenSpellcross** – postupná rekonstrukce toho nejtěžšího:
- načítání map a datových formátů
- základní herní mechaniky
- rekonstrukce obrazovek mimo mapu (Strategic level, Hierarchy, …)
- postupně i “campaign / progression” logika

Právě pro rekonstrukci UI a “asset pipeline” vznikla většina přiložených Python utilit.

---

## Co je hotové / v jakém stavu to je

### Map editor + game mode (wxWidgets/C++)
- **Game mode na mapě**
  - možnost přidat jednotky na načtenou mapu a mapu „hrát“
  - možnost hrát původní mise
  - **save/load stavu** rozehrané hry
  - jednoduchá **AI pro nepřátelské jednotky** (boj + reakce nepřítele) - AI je nedodělaná a obtížnost triviální!
- **Skupinový pohyb hráčských jednotek (Group move)**
  - přepínání aktivní skupiny a výběr jednotek do skupin
  - hromadný přesun více jednotek jedním klikem
  - pozn.: pořád se ladí okrajové stavy (např. kolize/stackování na jednom políčku)
- **Strategic UI (rozpracováno)**
  - `StrategicLevelFrame` + layout / background experimenty (transparentní wx prvky a PNG pozadí)
  - `HierarchyCanvas` (ručně poskládané „stromové“ UI s absolutním rozmístěním)
- **Ukládání / cesty**
  - řeší se sjednocení cest pro save/load (některé verze používají `temp/COMMON` vs `save/` – může způsobovat „přetahování“ stavu)
- **Původní / main menu**

> Pozn.: Game mode je použitelný, ale **není stabilní** – občasné pády a nedodělky jsou očekávané.

---

## Struktura repa (orientačně)

- `spellcross-map-edit-main/` – C++/wxWidgets editor + game mode (hlavní appka)
- `spell_extract_fs_gui/` – GUI pro rozbalení `.FS` archivů
- `spell_decomp/` – nástroje pro dekompresi (LZ/LZ0/DELZ)
- `bin_inspector/` – rychlá inspekce a extrakce obsahu z binů
- `spellcross_level_tool_v5/` – skládání a rekonstrukce map/levelů
- `bin_out/spell_ui_builder/` – skládání UI obrazovek z vytažených podkladů
- různé `*_gui.py` a helpery – experimenty a dílčí pipeline kroky

Názvy a umístění se mohou měnit – repo je živé a některé části jsou “workbench”.

---

## Build (Windows / Visual Studio)

### Požadavky
- **Visual Studio 2022/2026** (MSVC, C++ toolchain)
- **wxWidgets** (doporučeně buildnuté pro MSVC stejně jako projekt)

### Poznámky k build problémům
- Pokud narazíš na linker chyby typu **LNK2005/LNK1169** (duplicitní symboly),
  zkontroluj, že implementace UI tříd (např. `StrategicLevelFrame`) je jen v **jednom** `.cpp` souboru
  a že nedržíš omylem dvě kopie stejné implementace ve více translation units
  (typicky dvě verze `form_*.cpp` zároveň v projektu).
  
pokud to chcete zkoušet zprovoznit vše podstatné je v - `spellcross-map-edit-main/` – C++/wxWidgets editor + game mode (hlavní appka)
zbytek je python garbage

---

## Pipeline: jak z toho dostat data a výsledky

Typický postup (doporučené kroky):

1. **Rozbalit FS soubory**  
   `spell_extract_fs_gui`

2. *(volitelné, ale hodně pomáhá)* **Roztřídit data pro orientaci**  
   `data_sorter.py` – třídí data do složek podle typu / přípon

3. **Extrahovat LS a LS0 soubory → vzniknou biny**  
   `spell_decomp/spell_bulk_delz_gui.py`

4. **Zjistit, co je uvnitř binů a případně extrahovat**  
   `bin_inspector`

5. **Zpětně komponovat mapy z levelů**  
   `spellcross_level_tool_v5`

6. **Rekonstrukce herních menu/UI z vyextrahovaných podkladů**  
   `bin_out/spell_ui_builder`

---

## Utility na rekonstrukci grafiky Spellcrossu

### `unlz_gui.py`
- Rozbaluje **LZ** a **LZ0** soubory do binárek `*.bin`
- Výstup je vždy binárka – je na uživateli rozhodnout, co to je a jak to dál zpracovat  
  (raw grafika, midi, …)

### `spell_rawimg_guy_v2.py`
- Funkční řešení pro “klasické” raw obrázky
- Vstup: rozbalený `*.bin`
- Je potřeba vybrat správnou **paletu barev**
- Výstup: `*.png`

### `spell_rawimg_gui_v4.py` + `spell_rawimg_tool_v4.py`
- Experimentální řešení pro **skládanou raw grafiku**
- Vstup: rozbalený `*.bin`
- Vybere se paleta barev
- Výstup: `*.png`
- Je potřeba výsledek prohlédnout a “intuicí” doladit:
  - otáčení
  - prokládání (interleave)
  - případně další parametry

### Ostatní soubory
Zbytek jsou různé experimenty – některé funkční, některé méně, některé nefunkční.  
Ber to jako pracovní poznámky a výzkumné skripty.

---

## Známé limity / poznámky
- Game mode v editoru je použitelný, ale **není stabilní** (občasné pády).
- Skupinový pohyb a některé výjimky v logice pohybu (kolize/stackování) jsou pořád ve vývoji.
- Ukládání stavu (save/load) se stále sjednocuje – pokud se ti hra „pere“ sama se sebou,
  zkontroluj, odkud se načítá a kam zapisuje.
- Část Python utilit je **experimentální** a může vyžadovat ruční zásahy / ladění.
- Palety a raw grafika často vyžadují trpělivost – některé formáty jsou složené a bez “kontextu” se špatně hádají.

---

## Jak přispět
- Issue / popis problému: ideálně přiložit vzorek souboru + screenshot očekávaného výsledku
- PR vítané (čisté refactory, stabilizace pipeline, doplnění dokumentace)

---

## Co s tím bude dál?
- Postupně: stabilizace game modu, sjednocení save/load, a dokončení strategických obrazovek.
- A pak se uvidí. :)

---

## Licence
Tento projekt je licencován pod **MIT licencí** – viz soubor [`LICENSE`](LICENSE).
