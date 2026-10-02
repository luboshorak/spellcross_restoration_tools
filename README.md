# Spellcross Reloaded

Fanouškovská open-source rekonstrukce a reimplementace **Spellcross: Poslední bitva** s cílem dostat původní hru do podoby, která je znovu pohodlně hratelná na moderním Windows a přitom si co nejvíc zachovává původní vzhled, data a herní logiku.

Projekt vznikl jako fork / rozšíření původního map editoru, ale dnes už je jeho hlavním cílem **samotná hra**:

- hratelná taktická bojová mapa,
- kampaň a přechod mezi taktickou a strategickou částí,
- rekonstruovaná strategická vrstva ve stylu původního Spellcrossu,
- práce s jednotkami, veliteli, výzkumem, zdroji a statistikami,
- nástroje pro reverse engineering, extrakci a rekonstrukci původních dat a grafiky.

> **Stav projektu:** aktivní WIP / experimentální build. Hra je použitelná a velká část kampaně a strategické vrstvy už funguje, ale stále nejde o hotový ani plně otestovaný remake.

---

## Release

Poslední veřejně publikovaný build:

[**`Spellcross_Reloaded_v0.0.4`**](https://github.com/luboshorak/spellcross_restoration_tools/releases/tag/v0.0.4)

`main` je vývojová větev a může obsahovat novější změny než poslední release.

Aktuální vývojový cíl zůstává stejný: dostat Spellcross do stavu, kdy je možné **odehrát celou kampaň od začátku do konce** bez nutnosti vracet se k editorovým nebo debugovacím postupům.

Projekt je vyvíjen a testován především na **Windows 11**.

Počítejte s tím, že:

- build může obsahovat chyby a nehotové okrajové stavy,
- některé herní hodnoty a balancing ještě nemusí odpovídat originálu,
- některé mechaniky se stále ověřují proti původní hře a dokumentaci,
- save z vývojové verze nemusí být vždy kompatibilní s budoucími změnami.
- import originálních strategických save Spellcrossu `BIG_MAP.SAV` (automatická detekce kapitoly a převod do interního stavu),

---

## Co je Spellcross Reloaded dnes

Původní projekt byl map editor s experimentálním `game mode`. Spellcross Reloaded z tohoto základu postupně vyrostl v pokus o **otevřenou rekonstrukci celé hry**.

Editor v projektu stále existuje a je důležitý pro práci s mapami a původními daty, ale není už hlavním produktem. Primární směr je dnes:

1. zprovoznit původní kampaň,
2. rekonstruovat strategickou část hry,
3. propojit strategickou a taktickou vrstvu,
4. postupně zpřesňovat původní herní mechaniky,
5. zachovat vzhled a atmosféru původního Spellcrossu místo vytváření moderního redesignu.

Jinými slovy: cílem už není „editor, ve kterém se dá trochu hrát“, ale **Spellcross, který se dá znovu normálně hrát**.

---

## Aktuálně implementované části

### Taktická bojová mapa

Základ projektu stále tvoří původní C++/wxWidgets mapový engine a game mode.

Aktuálně je možné mimo jiné:

- načítat a hrát původní mapy a mise,
- pracovat s původními jednotkami a mapovými daty,
- pohybovat jednotkami a provádět útoky,
- používat základní mission/event logiku,
- vyhodnocovat cíle misí,
- přecházet mezi taktickou a strategickou částí hry,
- ukládat a načítat rozehraný stav,
- používat skupinový pohyb hráčských jednotek,
- hrát proti základní AI nepřátelských jednotek.

Taktická část je hratelná, ale AI, balancing a některé méně běžné mise či události stále potřebují další práci a testování.

---

## Strategická část hry

Strategická vrstva už není jen několik provizorních debug oken. Postupně byla rekonstruována jako skutečné herní rozhraní ve stylu originálu a používá společný stav kampaně.

Aktuálně jsou implementované nebo rozpracované zejména tyto části:

### Strategická mapa

- zobrazení aktuální kapitoly / strategické mapy,
- jednotlivá území a jejich stav,
- textové informace k misím,
- výběr jednotek pro útok,
- přechod do taktické mise,
- společné údaje o penězích, výzkumu a strategickém kole.

### Bojová hierarchie

- seznam jednotek a velitelů,
- zařazování jednotek do bojových formací,
- práce s veliteli,
- vizuální rekonstrukce původní obrazovky hierarchie.

### Řízení jednotek

- správa stálých jednotek,
- práce s poškozenými / dočasně nedostupnými jednotkami,
- příprava mechanik doplňování, úprav a přezbrojení,
- napojení jednotek na společný stav kampaně.

### Nákup jednotek a velitelů

- rozdělení jednotek do původních kategorií,
- ceny a čas potřebný pro získání jednotky,
- omezení dostupnosti podle aktuálního stavu kampaně,
- nákup nových jednotek a velitelů.

### Výzkum

- původní kategorie výzkumu,
- seznam dostupných výzkumných položek,
- spuštění / zastavení výzkumu,
- průběh výzkumu v čase,
- odemykání technologií, jednotek a vylepšení podle campaign state.

### Komplexní informace

- procházení informací o výzkumu, technologiích, vylepšeních, rasách a jednotkách,
- zobrazení původních textových informací ve stylu původní hry.

### Správa území a zdrojů

- zobrazení obsazených území,
- strategické body / produkce území,
- rozdělování zdrojů mezi **peníze** a **výzkum**,
- aktualizace zdrojů mezi strategickými koly.

### Statistiky

- ztráty Aliance a Other Side,
- statistika celé hry i aktuální kapitoly,
- hodnost a zkušenost Johna Alexandra,
- limity stálých jednotek a velitelů.

### Nastavení a save/load

- více save slotů,
- ukládání a načítání strategického stavu,
- gamma correction,
- hlasitost hudby a zvuků,
- volba rozlišení bojové mapy,
- Quick Help.

---

## Věrnost původní hře

Jedním z hlavních cílů není Spellcross „předělat“, ale **co nejvěrněji zrekonstruovat jeho původní chování a rozhraní**.

Proto projekt využívá:

- původní datové formáty,
- původní mapy a definice misí,
- extrahované grafické prvky původního UI,
- rekonstrukci rozložení strategických obrazovek,
- původní texty a herní data tam, kde jsou dostupné,
- původní manuál a reálné chování DOS verze jako referenci při obnovování mechanik.

Ne všechno je zatím 1:1. V řadě míst bylo nutné nejprve vytvořit funkční implementaci a teprve potom ji zpřesňovat podle originálu.

---

## Co ještě není hotové

Projekt je stále ve vývoji. Největší otevřené oblasti jsou zejména:

- další stabilizace campaign flow,
- okrajové podmínky jednotlivých misí a eventů,
- přesnější chování AI nepřátel i aliančních jednotek,
- dokončení a ověření všech vazeb mezi strategickou a taktickou vrstvou,
- úplné dotažení jednotkových upgradů, přezbrojení a souvisejících časových stavů,
- ověření všech typů pomocných a dočasných jednotek,
- přesnější balancing peněz, výzkumu, cen a časů,
- další porovnávání s původní DOS verzí,
- stabilita přehrávání některých původních multimediálních formátů,
- cleanup kódu, který stále nese část historie původního editoru a experimentálních implementací.

README se snaží popisovat stav `main`; konkrétní release může být proti němu o něco pozadu.

---

## Map editor

Map editor je původní technologický základ celého projektu a stále zůstává součástí repozitáře.

Umí mimo jiné:

- načítat a ukládat mapové `DTA` / `DEF` soubory,
- zobrazovat a upravovat terén, objekty, animace a jednotky,
- pracovat s eventy a mission objectives,
- zobrazovat a exportovat různé herní resources,
- používat původní datové formáty Spellcrossu.

Další výrazný vývoj editoru jako samostatného produktu ale **není hlavní prioritou Spellcross Reloaded**. Pro čistě editorový vývoj je důležitý především původní projekt Stanislava Mašláně.

---

## Struktura repozitáře

Aktuální hlavní části repa:

- `spellcross-map-edit-main/` – hlavní C++/wxWidgets aplikace, mapový engine, game mode a rekonstruovaná hra,
- `spellcross-master-pytools/` – Python workbench a utility pro analýzu, extrakci a rekonstrukci herních dat / UI,
- `spell_decomp/` – nástroje pro dekompresi původních datových formátů,
- `spell_extractfs/` – nástroje pro práci s původními `.FS` archivy.

Repo je stále živý vývojový workspace, takže umístění a názvy pomocných utilit se mohou měnit.

---

## Reverse engineering / data pipeline

Vedle samotné hry obsahuje projekt řadu pomocných Python nástrojů, které vznikly při rozebírání původních dat Spellcrossu.

Typický pracovní postup je zhruba:

1. rozbalit původní `.FS` / `.FSU` archivy,
2. dekomprimovat datové bloky (`LZ`, `LZ0`, `DELZ` a další),
3. prohlédnout a určit obsah vzniklých binárních souborů,
4. extrahovat grafiku, palety, texty nebo další resources,
5. rekonstruovat mapy a strategické obrazovky,
6. porovnat výsledek s původní hrou,
7. implementovat chování přímo do C++ části Spellcross Reloaded.

Část Python utilit je univerzálně použitelná, část jsou jednorázové nebo experimentální nástroje vytvořené pro konkrétní krok reverse engineeringu.

---

## Build

### Požadavky

- **Windows**
- **Visual Studio 2022 nebo novější**
- **wxWidgets** buildnuté pro odpovídající MSVC toolchain
- C++ toolchain odpovídající projektu

Hlavní aplikace je v:

```text
spellcross-map-edit-main/
```

Projekt vychází z původního wxWidgets editoru, takže při ručním buildu je nejčastější komplikací správné nastavení knihoven a cest k wxWidgets.

Pokud narazíte na linker chyby typu `LNK2005` / `LNK1169`, zkontrolujte zejména duplicitní implementace tříd nebo zdrojové soubory přidané do projektu vícekrát.

---

## Debug / Game Mode

Game mode je dnes součástí běžného workflow hry a není už jen experimentální funkce editoru.

Pro vývoj a debugging ale stále existují interní / konzolové možnosti. Některé z nich se mohou mezi verzemi změnit a nejsou považovány za stabilní veřejné rozhraní.

---

## Poděkování / Credits

Obrovské díky patří **Stanislavu Mašláňovi** – bez jeho práce by tenhle projekt prakticky neměl z čeho vyrůst.

Původní map editor, velká část reverse engineeringu datových formátů a řada nástrojů jsou jeho práce.

Jeho utility:

- https://spellcross.kvalitne.cz/

Originální map editor:

- https://github.com/smaslan/spellcross-map-edit

Spellcross Reloaded na této práci staví a posouvá původní editorový základ směrem k rekonstrukci celé hry.

---

## Jak přispět

Bug reporty a pull requesty jsou vítané.

U issue je ideální přiložit:

- přesný postup reprodukce,
- save game,
- screenshot,
- název mise / strategické kapitoly,
- případně vzorek problematického původního souboru.

Nejvíc pomůže práce na:

- stabilitě campaign flow,
- porovnávání mechanik s originálem,
- AI,
- save/load,
- jednotkových a strategických mechanikách,
- reverse engineeringu dosud nejasných formátů,
- dokumentaci a cleanupu kódu.

---

## Licence a původní herní data

Zdrojový kód vytvořený v rámci tohoto projektu je licencován pod **MIT licencí** – viz [`LICENSE`](LICENSE), pokud není u konkrétní části uvedeno jinak.

**Spellcross**, původní herní grafika, zvuky, hudba, texty, data a další původní obsah hry nejsou tímto repozitářem relicencovány. Práva k původní hře a jejím assetům zůstávají jejich příslušným držitelům.

Projekt je nekomerční fanouškovská rekonstrukce a není oficiálním produktem původních autorů ani vydavatele.
