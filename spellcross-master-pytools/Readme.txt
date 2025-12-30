Utility na rekonstrukci grafiky hry Spellcross

unlz_gui.py
rozbaluje LZ a LZ0 soubory do binárek *.bin
výstup je vždy binárka je na uživateli aby rozhodl co to je a jak to dál zpracovat
- raw grafika, midi soubory atd.

spell_rawimg_guy_v2.py
funkční řešení pro klasické raw obrázky
vstupní je rozbalený bin
pak je třeba vybrat správnou paletu barev
výstup je png

spell_rawimg_gui_v4.py + spell_rawimg_tool_v4.py
experimentální řešení pro skládanou raw grafiku
vstupní je rozbalený bin
pak je třeba vybrat správnou paletu barev
výstup je png
je potřeba si png prohlédnout a intuitivně nastavit otáčení a prokládání

zbytek souborů jsou různé experimenty více či méně nefunkční