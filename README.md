# Spellcross – restoration tools + úpravy map editoru (OpenSpellcross experiment)

Tohle repo vzniklo jako fanouškovský pokus **oživit a zpřístupnit starý Spellcross** modernějším způsobem: nejdřív tím, že se v map editoru podařilo reálně rozběhat *game mode* (hraní na editované mapě), a následně tím, že kolem toho vznikla sada utilit na **extrakci, dekompresi a rekonstrukci herních dat a UI obrazovek**.

> Stav: **WIP / experiment**. Některé části jsou stabilní, jiné jsou “research tooling” a mohou být rozbité nebo nedodělané.

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

Původní myšlenka byla **oživit game mod v editoru** (viz credits výše) – to se podařilo.  
Na to se nabalila další věc: udělat něco jako **OpenSpellcross** – tj. postupně zrekonstruovat to nejtěžší:
- **načítání map**
- **základní herní mechaniky**
- a navrch postupně **rekonstrukci obrazovek mimo mapu (UI)**

Právě pro rekonstrukci UI a “asset pipeline” vznikla většina přiložených Python utilit.

---

## Co je hotové

### Úpravy v map editoru
- **Upravený game mode**
  - možnost přidat jednotky na načtenou mapu
  - po zapnutí game modu jde mapu “hrát”
  - **save/load stavu** rozehrané hry
  - jednoduchá **AI pro nepřátelské jednotky** (můžete bojovat a nepřítel reaguje)
  - pozn.: je to **buggy** a občas to umí **spadnout**

- Drobné úpravy načítání dat
  - odstraněno pár bugů v načítání grafiky a `data/def` souborů

---

## Pipeline: jak z toho dostat data a výsledky

Typický postup (doporučené kroky):

1. **Rozbalit FS soubory**  
   `spell_extract_fs_gui`

2. *(volitelné, ale hodně pomáhá)* **Roztřídit data pro orientaci**  
   `data_sorter.py` – třídí data do složek podle typu / přípon

3. **Extrahovat LS a LS0 soubory → vzniknou biny**  
   `..\spell_decomp\spell_bulk_delz_gui.py`

4. **Zjistit, co je uvnitř binů a případně extrahovat**  
   `bin_inspector`

5. **Zpětně komponovat mapy z levelů 02–10**  
   `spellcross_level_tool_v5`

6. **Rekonstrukce herních menu/UI z vyextrahovaných podkladů**  
   `bin_out\spell_ui_builder`

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
- Část Python utilit je **experimentální** a může vyžadovat ruční zásahy / ladění.
- Palety a raw grafika často vyžadují trpělivost – některé formáty jsou složené a bez “kontextu” se špatně hádají.

---

## Jak přispět
- Issue / popis problému: ideálně přiložit vzorek souboru + screenshot očekávaného výsledku
- PR vítané (čisté refactory, stabilizace pipeline, doplnění dokumentace)

---

## Co s tím bude dál?
- Těžko říct, asi se k tomu budu náhodně vracet podle nálady, třeba to někdy dodělám, ale spíš ne.

---

## Licence

Tento projekt je licencován pod **MIT licencí** – viz soubor [`LICENSE`](LICENSE).
