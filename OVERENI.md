# Ověření

Každé tvrzení, na kterém firmware stojí, a z čeho vychází. Stav k 17. 10. 2026.

## Domovská obrazovka a nastavení (verze 2.4, výpis přes USB, 17. 10. 2026)

| Zjištění | Stav |
|---|---|
| Dotyk na hodiny otevře domov, koule dole spustí rozhovor, po rozhovoru se vrátí na domov | viděno ve výpisu, dotyky posílané servisní konzolí (`tap X Y`) |
| Nastavení uspání se uloží a přežije restart | viděno ve výpisu |
| Otočení 90°, 180°, 270° a zpět | bez pádu a bez zpomalení; prstence a koule otočené bod po bodu stejně jako neotočené (porovnáno v náhledu na Macu); na displeji **neviděno** |
| Čas snímku | hodiny 9 až 10 ms, domov 2 ms, rozhovor kolem 25 ms; 29 snímků za sekundu |
| Volná vnitřní paměť | v klidu 73 až 93 kB, při rozhovoru 50 až 64 kB |
| Vzhled domova a nastavení | viděno jen v náhledu na Macu (stejný kód LVGL přeložený pro počítač) |
| Dotyk prstem po otočení, uspání do tmy, ovládání světel z dlaždic | **neověřeno** (hub nespárovaný) |
| Verze 2.4.1: hlášení „Nepodařilo se otevřít spojení“ u uživatele | příčinu jsem nezachytil (deska se při otevření výpisu restartuje); 50 otevření nastavení a 8 otočení paměť nevyčerpalo (souvislý blok 32 kB). Připojení se teď zkouší až třikrát a chyba se vypisuje i s kódem TLS a socketu |
| Verze 2.4.1: lampička svítila jen jasem z nastavení | opraveno, při lampičce je jas vždy 100 % |

## Co ukázala skutečná deska (verze 2.3, výpis přes USB, 11. 10. 2026)

| Zjištění | Stav |
|---|---|
| Start bez pádu, 8 MB PSRAM, displej ST7701, dotyk GT911, ES8311 a ES7210 na 16 kHz, model `wn9_jarvis_tts` | viděno ve výpisu; jednou při startu selhala komunikace s expandérem TCA9554 a deska se sama restartovala |
| Volná vnitřní paměť | v klidu 85 kB, při rozhovoru 73 kB (před přesunem velkých polí do PSRAM klesala na 10 kB) |
| Mikrofon | klid v tiché místnosti do 250, řeč z reproduktoru počítače 3 000 až 30 000 z 32 767 |
| Oslovení | „Jarvis“ (anglický i český syntetický hlas) vždy; „Džárvisi“ při prahu 0,63 nikdy, při 0,48 ve 3 ze 4 pokusů, při 0,42 častěji, ale s planými poplachy v tichu. Výchozí práh 0,48 |
| Plané poplachy v tichu | 25 minut v tiché místnosti při prahu 0,48: žádné oslovení, 29 snímků za sekundu po celou dobu, žádný restart |
| Zamrzání po oslovení (verze 2.2) | kreslení při rozhovoru trvalo déle než snímek a vyhladovělo řídicí smyčku; opraveno pauzou po každém snímku, vyšší prioritou řízení a rychlejším kreslením |
| Vytížení procesoru | jádro 0 kolem 10 %; jádro 1 v klidu 22 % (dřív 50 %), při řeči kolem 70 % (dřív 83 %) |
| Gemini Live z desky | odpovídá hlasem; klíč nového tvaru `AQ.…`; bezplatný tarif model `gemini-3.8-live` povoluje, s nástrojem hledání spojení zavře hláškou o kvótě |
| Trhaná řeč | uživatel slyšel výpadky po slovech; síla Wi-Fi tehdy −71 dBm a stahování 140 až 190 kB/s s pauzami až 0,7 s. Po zvětšení síťových bufferů, zapnutí SACK a při signálu −55 dBm 690 kB/s a pauzy do 41 ms. Co z toho pomohlo víc, nevím |
| Zásobník zvuku 300 ms a měření výpadků | při tichých zkouškách 0 až 1 výpadek na odpověď, nejdelší pauza v příjmu 110 až 440 ms |
| Počasí, připomínka přes nástroj, ohlášení v čas, opakování po 90 s, potvrzení dotykem | viděno ve výpisu, reproduktor ztlumený |
| Vzhled animací po odlehčení | **neviděno** na displeji; při vypnutých prazích dává nový kód bod po bodu stejný obraz jako původní |
| Poslech výsledné řeči | **neověřeno** |

## Deska

| Tvrzení | Zdroje | Stav |
|---|---|---|
| I2C SDA 47, SCL 48 | schéma 4B (tabulka pinů); BSP `waveshare/esp32_s3_touch_lcd_4b` 2.0.0; `pin_config.h` v repu výrobce | shoda |
| I2S MCLK 5, BCLK 16, LRCK 7, data do reproduktoru 6, data z mikrofonů 15 | schéma (I2S_MCLK, I2S_DSDIN, I2S_LRCK na IO5–7, I2S_ASDOUT a I2S_SCLK na IO15–16); BSP; `pin_config.h` | shoda |
| Zesilovač NS4150B se zapíná úrovní 1 na EXIO3 | schéma (CTRL ← EXIO3, 10k k zemi); BSP `bsp_audio_poweramp_enable`; příklady výrobce | shoda |
| Mikrofony jsou na vstupech MIC1 a MIC2 kodeku ES7210 | schéma (MIC1_P/N, MIC2_P/N); ovladač `esp_codec_dev` je vybírá jako výchozí | shoda |
| Displej ST7701, inicializace přes expandér TCA9554 (EXIO0–2), reset dotyku EXIO5, INT EXIO6 | schéma; BSP; dvě nezávislé funkční konfigurace ESPHome z fóra Home Assistanta | shoda |
| Podsvícení GPIO4, obrácené PWM | schéma (BL na IO4); BSP `bsp_display_brightness_set`; konfigurace z fóra | shoda |
| Flash 16 MB QIO, PSRAM 8 MB oktální | wiki výrobce; `sdkconfig.defaults` z příkladu výrobce | shoda |

Displej, dotyk a zvuk firmware neovládá vlastním kódem, ale přes BSP výrobce.

## Oslovení

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Existuje hotový model na slovo „Jarvis“ pro ESP32-S3 | soubor `model/wakenet_model/wn9_jarvis_tts` v `espressif/esp-sr` 2.6.0; `wakeword_list.md`; volba `CONFIG_SR_WN_WN9_JARVIS_TTS` v Kconfig | shoda |
| Model je v obrazu, který se nahrává | `tools/release.py` hledá `wn9_jarvis_tts` v `srmodels.bin` | kontrolováno při každém vydání |
| Volání AFE (`afe_config_init`, `feed`, `fetch`) | hlavičky esp-sr 2.6.0; funkční projekt `AnkitVats21/waveshare` | shoda, přeloženo |
| Reaguje i na české „Jarvisi“ | — | **neověřeno**, jen úvaha |

## Gemini Live

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Adresa `wss://generativelanguage.googleapis.com/ws/…BidiGenerateContent?key=` | dokumentace Googlu; knihovna `google-genai` 2.29 volá stejnou cestu; skutečný server spojení přijal a s neplatným klíčem odpověděl „API key not valid“ | ověřeno naostro |
| Tvar zpráv, které deska posílá (setup, zvuk, konec proudu, odpověď nástroje) | dokumentace; knihovna `google-genai` zachycená na falešném serveru (`firmware/test/sdk_wire.py`); projekt `mk14ray/RIO…` běžící na ESP32-S3 | strukturně shodné, test |
| Tvar zpráv, které deska čte (zvuk, přepisy, volání nástroje, konec tahu) | dokumentace; tatáž sada zpráv přečtená firmwarem i knihovnou `google-genai` se stejným výsledkem; projekty `vinhdn/esp32s3-gemini` a `RIO` | shodné, test |
| Zvuk tam 16 kHz PCM, zpět 24 kHz PCM | dokumentace Live API; oba projekty | shoda |
| Model `gemini-3.8-live`, hlasy Charon, Orus, Puck… | dokumentace Live API a seznam hlasů | podle dokumentace |
| Jde spojit hledání na Googlu s vlastními nástroji | dokumentace nástrojů Live API | podle dokumentace; když server nastavení odmítne, další pokus jde bez hledání |
| Rozhovor naostro | `firmware/test/live_check.py` žene kód firmwaru proti skutečnému serveru; zatím proběhl jen proti místní napodobenině | **čeká na klíč** |

## IKEA DIRIGERA

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Hub je v síti a hlásí se službou `_ihsp._tcp` na portu 8443 | mDNS záznam hubu v téhle síti (`type=DIRIGERA`, 10.0.0.16) | viděno |
| Párování: `/v1/oauth/authorize` s PKCE, stisk tlačítka, `/v1/oauth/token` | knihovna `Leggin/dirigera` (Python); `lpgera/dirigera` (TypeScript); článek cacodaemon.de | shoda dvou knihoven |
| Zapnutí: `PATCH /v1/devices/{id}` s `[{"attributes":{"isOn":true}}]`, jas `lightLevel` | obě knihovny | shoda |
| Pole `id`, `type`, `attributes.customName`, `room.name`, `isReachable` | modely obou knihoven | shoda |
| Volání proti hubu | — | **neověřeno**, macOS mi přístup do místní sítě nepovolil |

## Tiskárna Bambu Lab A1

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Místní spojení: MQTT přes TLS na portu 8883, uživatel `bblp`, heslo je přístupový kód | dokumentace `Doridian/OpenBambuAPI` (mqtt.md); integrace `greghesp/ha-bambulab` (`bambu_client.py`); firmware `xperiments-in/xtouch` pro ESP32 | shoda tří zdrojů |
| Stav chodí na `device/{sériové číslo}/report`, žádost o celý stav je `pushall` na `device/{sériové číslo}/request` | tytéž tři zdroje; zdroják Bambu Studia (`DeviceManager.cpp`, `command_pushing`) | shoda |
| A1 posílá jen změny, celý stav až na `pushall` | OpenBambuAPI; ha-bambulab | shoda; firmware změny slučuje |
| Pole `gcode_state`, `mc_percent`, `mc_remaining_time` (minuty), `layer_num`, `total_layer_num`, `subtask_name`, `stg_cur`, `print_error`, `hms` | OpenBambuAPI (ukázka zprávy); ha-bambulab (`models.py`); xtouch (`mqtt.h`) | shoda; čísla umí přijít i jako text, firmware bere obojí |
| Čísla fází tisku (35 = pauza kvůli ucpané trysce, 6 = došel filament…) | Bambu Studio (`get_stage_string`); ha-bambulab (`CURRENT_STAGE_IDS`) | shoda |
| Kód hlášení HMS je `attr` a `code` jako 16 šestnáctkových číslic, kód `print_error` 8 číslic | ha-bambulab (`HMSList`, `PrintError`); xtouch | shoda, test |
| Text k chybovému kódu vrací `https://e.bambulab.com/query.php?lang=en&e=KÓD` | vyzkoušeno naostro z tohoto Macu pro kód HMS i `print_error`; odkaz ve zdrojáku ha-bambulab | ověřeno naostro; česky server neumí, překládá Gemini |
| Tiskárna se v síti ohlašuje na UDP portu 2021 zprávou se službou `urn:bambulab-com:device:3dprinter:1`, adresou v `Location` a sériovým číslem v `USN` | Bambu Studio na tomhle Macu na portu 2021 poslouchá; manifest ha-bambulab (`ssdp`); skript `gashton/bambustudio_tools` | shoda; samotnou zprávu jsem nezachytil, port drží Bambu Studio |
| Certifikát tiskárny se neověřuje | ha-bambulab a xtouch dělají totéž (vlastní certifikační autorita Bambu, spojení na IP adresu) | vědomé zjednodušení |
| Spojení se skutečnou tiskárnou | — | **neověřeno** |

## Kalendář a cesta

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Čtení souboru iCal včetně opakování, výjimek a přesunutých výskytů | 61 kalendářů (ruční ve stylu iCloudu a 60 náhodných) porovnáno s knihovnou `recurring_ical_events` v pěti časových oknech včetně obou změn času; ruční kalendář navíc krmený po 1, 2, 3, 7 a 61 bajtech | bez rozdílu, `firmware/test/agenda_check.py` |
| Veřejný kalendář z iCloudu jde stáhnout z desky | — | **neověřeno**, odkaz nemám |
| Vyhledání adresy přes Nominatim a trasa přes `routing.openstreetmap.de` (kolo, pěšky, auto) | obě služby vyzkoušené naostro z tohoto Macu, odpovědi čte stejný kód jako na desce | ověřeno naostro z Macu, z desky ne |
| Čas na elektrokoloběžce | délka cyklotrasy při 17 km/h | odhad, ne měření |

## Připomínky a vlastní promluva

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Jarvise jde přimět promluvit první zprávou `clientContent` s `turnComplete` | dokumentace Live API (podporováno po celou dobu spojení); knihovna `google-genai` posílá stejný tvar (`firmware/test/sdk_wire.py`) | tvar shodný, naostro **neověřeno** |
| Ukládání, opakování po 90 s, nejvýš čtyři pokusy, přežití restartu | `firmware/test/test_modules.c` | test |
| Za odpověď se počítá, když Gemini v naslouchání po promluvě přepíše aspoň dvě písmena, nebo dotyk | — | **neověřeno** v pokoji; hluk nebo televize by se mohly počítat jako odpověď |

## Flasher

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Části se zapisují na 0x0, 0x8000, 0x10000, 0x810000 | `flasher_args.json` ze sestavení; tabulka oddílů přečtená v `release.py` | kontrolováno při každém vydání |
| Nahrání nepřepíše uložené nastavení ani spárování s hubem | `release.py` porovnává zapisované rozsahy s oddíly `nvs` a `cfg` | kontrolováno při každém vydání |
| Nastavení z formuláře dojde do desky ve tvaru, který firmware čte | v prohlížeči sestavený obraz 4096 B rozebrán stejným pravidlem jako ve firmwaru, včetně diakritiky, uvozovek, kódu tiskárny, kalendářů a adresy | ověřeno v prohlížeči |
| ESP Web Tools umí manifest z `blob:` adresy a s `new_install_improv_wait_time: 0` přeskočí Improv | zdroják přibalené verze 10.4.0 | přečteno |
| „Erase device“ je ve výchozím stavu nezaškrtnuté | zdroják přibalené verze | přečteno |
| Zápis do skutečné desky | — | **neověřeno** |

## Displej (verze 2.1)

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Ukazatel z `bsp_display_new` je RGB panel, ze kterého jde získat obrazová paměť | zdroják `esp_lcd_st7701_rgb.c` (přepisuje funkce téhož objektu); `esp_lvgl_port` na něm volá stejné funkce | přečteno |
| Přímý zápis do obrazové paměti se zobrazí bez dalšího kroku | zdroják `esp_lcd_panel_rgb.c`: s bounce bufferem kopíruje obraz procesor, synchronizace cache se nedělá | přečteno |
| Koule, prstence a okrajová záře vypadají jako v náhledu | stejný kód `fx.c` a `scene.c` vykreslený na Macu | viděno na Macu |
| Plynulost na desce | — | **neověřeno**, firmware vypisuje snímky za sekundu a čas snímku |

## Co kompilace ani testy neodhalí

- oslovení živým hlasem (zkoušeno jen syntetickým z počítače)
- jestli po odpovědi nezachytí mikrofon dozvuk reproduktoru (pauza 300 ms je odhad)
- zatížení paměti při souběhu displeje, Wi-Fi, TLS a rozpoznávání
- chování při slabé Wi-Fi
- jestli Jarvis s novou povahou opravdu mluví tak, jak má (vykání, „pane“, humor), a jestli sám nabídne připomenutí odchodu
- jak dlouho trvá stažení velkého kalendáře a kolik vnitřní paměti ubere spojení s tiskárnou
