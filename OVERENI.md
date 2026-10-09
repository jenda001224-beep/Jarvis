# Ověření

Každé tvrzení, na kterém firmware stojí, a z čeho vychází. Stav k 9. 10. 2026.

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
| Rozhovor naostro | — | **neověřeno**, nemám klíč |

## IKEA DIRIGERA

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Hub je v síti a hlásí se službou `_ihsp._tcp` na portu 8443 | mDNS záznam hubu v téhle síti (`type=DIRIGERA`, 10.0.0.16) | viděno |
| Párování: `/v1/oauth/authorize` s PKCE, stisk tlačítka, `/v1/oauth/token` | knihovna `Leggin/dirigera` (Python); `lpgera/dirigera` (TypeScript); článek cacodaemon.de | shoda dvou knihoven |
| Zapnutí: `PATCH /v1/devices/{id}` s `[{"attributes":{"isOn":true}}]`, jas `lightLevel` | obě knihovny | shoda |
| Pole `id`, `type`, `attributes.customName`, `room.name`, `isReachable` | modely obou knihoven | shoda |
| Volání proti hubu | — | **neověřeno**, macOS mi přístup do místní sítě nepovolil |

## Flasher

| Tvrzení | Zdroje | Stav |
|---|---|---|
| Části se zapisují na 0x0, 0x8000, 0x10000, 0x810000 | `flasher_args.json` ze sestavení; tabulka oddílů přečtená v `release.py` | kontrolováno při každém vydání |
| Nahrání nepřepíše uložené nastavení ani spárování s hubem | `release.py` porovnává zapisované rozsahy s oddíly `nvs` a `cfg` | kontrolováno při každém vydání |
| Nastavení z formuláře dojde do desky ve tvaru, který firmware čte | v prohlížeči sestavený obraz 4096 B rozebrán stejným pravidlem jako ve firmwaru, včetně diakritiky a uvozovek | ověřeno v prohlížeči |
| ESP Web Tools umí manifest z `blob:` adresy a s `new_install_improv_wait_time: 0` přeskočí Improv | zdroják přibalené verze 10.4.0 | přečteno |
| „Erase device“ je ve výchozím stavu nezaškrtnuté | zdroják přibalené verze | přečteno |
| Zápis do skutečné desky | — | **neověřeno** |

## Co kompilace ani testy neodhalí

- citlivost mikrofonů a oslovení v reálném pokoji
- jestli po odpovědi nezachytí mikrofon dozvuk reproduktoru (pauza 300 ms je odhad)
- zatížení paměti při souběhu displeje, Wi-Fi, TLS a rozpoznávání
- chování při slabé Wi-Fi
