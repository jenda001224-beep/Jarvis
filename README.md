# Jarvis — hlasový asistent na zeď

Waveshare **ESP32-S3-Touch-LCD-4B** (4" 480×480, vestavěné 2 mikrofony + reproduktor) jako hlasový satelit Home Assistanta. Mozek je Gemini, hudba hraje z Apple Music do Bluetooth reproduktoru.

## Stav

| Co | Stav |
|---|---|
| `esphome/jarvis.yaml` | `esphome config` bez chyb, `esphome compile` úspěšná (ESPHome 2026.9.1, firmware 1,7 MB, 21 % flash) |
| `docs/` (web flasher) | stránka, manifest i obraz ověřené v prohlížeči; samotné nahrání do desky netestováno |
| `homeassistant/packages/jarvis.yaml` | YAML i šablony zkontrolované parserem |
| Reálný hardware | **netestováno** — desku jsem neměl v ruce |

Piny jsou převzaté z oficiálního Waveshare BSP `esp32_s3_touch_lcd_4b` a ze schématu, displej podle dvou nezávisle fungujících ESPHome konfigurací z fóra Home Assistanta.

## Architektura

```
            ┌────────────────────── Wi-Fi ──────────────────────┐
            │                                                    │
┌───────────┴───────────┐  zvuk z mikrofonu   ┌──────────────────┴──────────────────┐
│ ESP32-S3-Touch-LCD-4B │ ──────────────────▶ │ Home Assistant                      │
│ ESPHome firmware      │                     │  Assist pipeline „Jarvis“            │
│  · wake word na čipu  │ ◀────────────────── │   1. STT  Gemini (čeština)          │
│  · LVGL displej       │  text + URL odpovědi│   2. LLM  Gemini + ovládání domu    │
│  · vestavěný reprák   │                     │   3. TTS  Gemini                    │
└───────────────────────┘                     │  Music Assistant + Apple Music      │
                                              │  Sendspin Bluetooth Bridge          │
                                              └───────┬──────────────────┬──────────┘
                                                      │ IKEA / Zigbee    │ Bluetooth A2DP
                                               IKEA světla, zásuvky   Bluetooth reproduktor
```

Proč takhle:

- **ESPHome, ne vlastní C++.** Wake word, přenos zvuku, LVGL i napojení na Home Assistant jsou hotové a udržované komponenty. Vlastní firmware by znamenal psát totéž znovu.
- **Gemini se volá z Home Assistanta, ne z ESP32.** API klíč zůstane na serveru, function calling řeší Assist a model jde vyměnit bez flashování.
- **ESP32-S3 neumí Bluetooth Classic**, takže na reproduktor nevidí. Bluetooth zvuk posílá stroj s Home Assistantem (doplněk Sendspin Bluetooth Bridge), reproduktor se pak tváří jako přehrávač Music Assistanta.
- **Odpovědi** jdou do Bluetooth reproduktoru jako oznámení (hudba se ztlumí a pak pokračuje). Když reproduktor není dostupný, odpoví vestavěný reproduktor desky. Přepíná se přepínačem `Odpovědi do Bluetooth reproduktoru`.

## Co je třeba vědět předem

1. **Wake word je „Hey Jarvis“.** Mezi oficiálními modely microWakeWord samotné „Jarvisi“ není. Jde natrénovat vlastní a vyměnit jedním řádkem `wake_word_model`.
2. **Text se na displeji objeví po dořeknutí věty**, ne slovo po slovu. Assist pipeline průběžný přepis neposílá. Během mluvení reaguje kruh na hlasitost.
3. **Free tier Gemini má denní limity** a jeden příkaz stojí 3 až 4 požadavky (STT, LLM, případně nástroj, TTS). Nejtěsnější bývá TTS. Aktuální čísla jsou jen v AI Studiu (Rate limits). Jednoduché povely typu „zhasni“ proto nech vyřizovat lokálně (krok 5), Gemini se na ně vůbec nevolá.
4. **Apple Music v Music Assistantu** vyžaduje placené předplatné, hraje nejvýš AAC 256 kb/s a Apple ho oficiálně nepodporuje.
5. Během odpovědi je mikrofon vypnutý (mikrofony a reproduktor desky sdílejí I2S sběrnici). Odpověď přerušíš dotykem displeje.

## Soubory

| Soubor | K čemu |
|---|---|
| `esphome/jarvis.yaml` | firmware: displej, dotyk, mikrofony, reproduktor, wake word, UI |
| `docs/` | web flasher (GitHub Pages): stránka, manifest, firmware, přibalené ESP Web Tools |
| `tools/release.py` | zkontroluje sestavený obraz a vygeneruje `docs/manifest.json` |
| `homeassistant/packages/jarvis.yaml` | nástroje pro Gemini: hudba, počasí, hledání na webu |
| `homeassistant/jarvis_prompt.txt` | instrukce hlavního agenta |
| `homeassistant/jarvis_web_prompt.txt` | instrukce vyhledávacího agenta |
| `homeassistant/stt_prompt.txt` | instrukce pro přepis řeči |

## Plán krok za krokem

Po každém kroku je kontrola. Dál jdi, až když projde.

### 1. Home Assistant a IKEA

Světla a zásuvky už v Home Assistantu máš. Dej je do oblasti **Pokoj** a poznamenej si ID entit (světlo, zásuvka u stolu, teploměr).

Kontrola: světlo jde přepnout z Home Assistanta.

### 2. Bluetooth reproduktor a Apple Music

1. Nainstaluj **Music Assistant** (Nastavení → Doplňky) a integraci Music Assistant. V jeho nastavení zapni poskytovatele přehrávačů **Sendspin**.
2. V Music Assistantu přidej poskytovatele **Apple Music**. Přihlášení dělej z adresy `http://IP_HA:8095`, jinak selže.
3. Přidej repozitář doplňků `https://github.com/trudenboy/sendspin-bt-bridge`, nainstaluj **Sendspin Bluetooth Bridge**, v jeho rozhraní spáruj reproduktor a propoj ho s Music Assistantem.
4. Přehrávač v Home Assistantu přejmenuj tak, aby měl ID `media_player.bluetooth_reproduktor`.

Stroj s Home Assistantem potřebuje Bluetooth. Pokud ten samý adaptér používá integrace Bluetooth, kup pro bridge samostatný USB adaptér, jinak se perou.

Kontrola: v Music Assistantu pusť skladbu z Apple Music do reproduktoru.

### 3. Gemini

1. Na `aistudio.google.com/app/apikey` vytvoř API klíč.
2. Nastavení → Zařízení a služby → Přidat integraci → **Google Gemini**, vlož klíč.
3. Integrace si založí výchozí položky. Smaž je nebo nech být a přidej tyto čtyři (ID entit se odvozují z názvu při založení, proto nové, ne přejmenované):

| Typ | Název | Nastavení |
|---|---|---|
| Conversation agent | `Jarvis` | Instructions = obsah `jarvis_prompt.txt`, Control Home Assistant = **Assist** |
| Conversation agent | `Jarvis Web` | Instructions = obsah `jarvis_web_prompt.txt`, Control Home Assistant = vypnuto, **Enable Google Search tool** = zapnuto |
| Speech-to-text | `Jarvis STT` | Prompt = obsah `stt_prompt.txt` |
| Text-to-speech | `Jarvis TTS` | výchozí |

Model nech na „Recommended“. Volbu Google Search odemkneš odškrtnutím „Recommended model settings“.

Druhý agent je nutný, protože Gemini neumí v jednom požadavku kombinovat Google Search s ovládáním domu. ID entity druhého agenta musí být `conversation.jarvis_web`.

Kontrola: Nastavení → Hlasoví asistenti → vyzkoušej agenta textem „kolik je hodin“.

### 4. Nástroje pro Jarvise

1. Do `configuration.yaml` přidej:

```yaml
homeassistant:
  packages: !include_dir_named packages
```

2. Zkopíruj `homeassistant/packages/jarvis.yaml` do `/config/packages/`.
3. Pokud se tvoje entity jmenují jinak, uprav v něm `media_player.bluetooth_reproduktor` a `weather.forecast_home`.
4. Restartuj Home Assistant.
5. Nastavení → Hlasoví asistenti → **Zpřístupnit** → přidej světlo, zásuvku, teploměr, přehrávač a tři skripty `Jarvis: …`. Entitám dej aliasy, jak jim říkáš („světlo“, „lampa“, „zásuvka u stolu“).

Kontrola: Vývojářské nástroje → Akce → `script.jarvis_play_music` s `media_id: Groovejet`, `media_type: track`. Musí začít hrát.

### 5. Hlasový asistent

Nastavení → Hlasoví asistenti → Přidat asistenta:

| Pole | Hodnota |
|---|---|
| Název | Jarvis |
| Jazyk | Čeština |
| Konverzační agent | Jarvis |
| Preferovat lokální zpracování příkazů | zapnuto |
| Převod řeči na text | Jarvis STT |
| Převod textu na řeč | Jarvis TTS, hlas vyber poslechem (např. Charon) |

Kontrola: v okně asistenta napiš „pusť Groovejet“ a „zhasni v pokoji a vypni zásuvku u stolu“.

### 6. Firmware

Firmware je jeden univerzální obraz bez hesel. Wi-Fi, klíč API i ID entit se nastavují až po nahrání.

1. Otevři web flasher **https://jenda001224-beep.github.io/Jarvis/** v Chrome nebo Edge na počítači.
2. Připoj desku USB-C kabelem do portu **UART** (ten, který není uprostřed) a klikni na **Nainstalovat Jarvise**.
3. Ve stejném okně pak zadej Wi-Fi. Když se nabídka neobjeví, připoj se telefonem k síti `Jarvis Setup` (heslo `jarvis-setup`) a nastav Wi-Fi tam.
4. Home Assistant zařízení sám najde, přidej ho. Šifrovací klíč si doplní sám.
5. V nastavení zařízení v integraci ESPHome zapni **Povolit zařízení provádět akce Home Assistanta**. Bez toho nefungují dlaždice na displeji ani odpovědi do Bluetooth.
6. U zařízení vyber asistenta **Jarvis**.
7. Na kartě zařízení v sekci Konfigurace vyplň ID svých entit (přehrávač, světlo, zásuvka, teplota). Výchozí hodnoty jsou `media_player.bluetooth_reproduktor`, `light.pokoj`, `switch.zasuvka_u_stolu` a `sensor.pokoj_teplota`. Po změně se displej restartuje.

Bez prohlížeče jde totéž z příkazové řádky:

```bash
cd /Volumes/Jan/Jarvis/esphome && ../.tools/venv/bin/esphome run jarvis.yaml
```

Kontrola po zapnutí, v tomhle pořadí:

| Co vidíš | Znamená |
|---|---|
| hodiny a tečka vpravo nahoře svítí modře | displej, Wi-Fi a API fungují |
| dotyk na dlaždici přepne světlo | dotyk a akce fungují |
| „Hey Jarvis“ → modrý kruh reaguje na hlas | mikrofony a wake word fungují |
| pod kruhem se objeví tvoje věta a odpověď | STT a Gemini fungují |
| odpověď zazní z reproduktoru | TTS a směrování zvuku fungují |

### 7. Na zeď

Napájení přes USB-C. Otvory mikrofonů po stranách nech volné a desku nedávej těsně k Bluetooth reproduktoru, ať se Jarvis neposlouchá sám.

## Když něco nejde

| Projev | Kam se podívat |
|---|---|
| černý displej, podsvícení svítí | log při startu, řádky `mipi_rgb` a `pca9554`; zkus `pclk_frequency: 8MHz` |
| růžové barvy | `invert_colors` a `pixel_mode` v sekci `display` |
| obraz bliká | sniž `pclk_frequency` |
| dotyk nereaguje | v logu `gt911`; zkus odebrat `interrupt_pin` |
| podsvícení pod 40 % bliká | zvyš `brightness_idle` |
| wake word nereaguje | v logu `micro_wake_word`; přepínač „Vypnout mikrofon“ musí být vypnutý |
| ticho z desky | hlasitost entity „Reproduktor“, přepínač Bluetooth odpovědí |
| „Něco se nepovedlo“ | Nastavení → Hlasoví asistenti → Jarvis → Ladění; nejčastěji vyčerpaný limit Gemini |

Log: `../.tools/venv/bin/esphome logs jarvis.yaml`

## Nová verze firmwaru

1. V `esphome/jarvis.yaml` zvyš `version`.
2. Sestav a vygeneruj manifest:

```bash
cd /Volumes/Jan/Jarvis/esphome && ../.tools/venv/bin/esphome compile jarvis.yaml && python3 ../tools/release.py
```

3. Commitni a pushni `docs/`. Web flasher nabídne novou verzi hned, zařízení si jí všimne do šesti hodin a v Home Assistantu ukáže aktualizaci firmwaru.

Skript odmítne obraz, který nemá správnou hlavičku pro ESP32-S3, nevejde se do oddílu nebo by obsahoval `!secret`.

## Ovládání na displeji

- dlaždice **Světlo** a **Zásuvka** přepínají zařízení
- dlaždice **Pokoj** spustí poslech bez wake wordu
- spodní lišta: přehrát/pauza a další skladba
- dotyk během poslechu nebo odpovědi ji zruší
- po minutě nečinnosti se displej ztlumí
