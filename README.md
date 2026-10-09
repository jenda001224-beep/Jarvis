# Jarvis — hlasový asistent na zeď

Deska Waveshare **ESP32-S3-Touch-LCD-4B** (4" displej, dva mikrofony, reproduktor) mluví přímo s Gemini. Doma neběží žádný server.

**Instalace z prohlížeče:** https://jenda001224-beep.github.io/Jarvis/

## Jak to funguje

```
 „Jarvisi, zhasni světlo“
        │
┌───────▼──────────────────┐   živý zvuk tam i zpět     ┌──────────────────┐
│ deska na zdi             │ ◀────────────────────────▶ │ Gemini Live      │
│  · pozná slovo „Jarvis“  │   (jedno šifrované spojení)│ (Google)         │
│  · mikrofon, reproduktor │                            └──────────────────┘
│  · displej               │   příkaz „zhasni“          ┌──────────────────┐
│                          │ ─────────────────────────▶ │ IKEA DIRIGERA    │
└──────────────────────────┘   (místní síť)             └──────────────────┘
```

1. Slovo „Jarvis“ pozná deska sama, bez internetu. Do té doby nic nikam neposílá.
2. Po probuzení otevře spojení na Gemini Live a posílá zvuk z mikrofonu. Co jsi stihl říct během připojování, se neztratí.
3. Gemini odpovídá rovnou hlasem. Na displeji běží přepis toho, co slyšel a co říká.
4. Když po něm chceš světlo, pošle zpátky příkaz a deska ho předá IKEA hubu.
5. Po odpovědi ještě deset vteřin poslouchá, jestli nechceš pokračovat. Pak spojení zavře.

## Stav

| Co | Stav |
|---|---|
| Firmware | přeložený bez varování (ESP-IDF 5.5.5), 2,2 MB |
| Zprávy pro Gemini | shodné s tím, co posílá oficiální knihovna Googlu; adresa ověřená proti skutečnému serveru |
| Web flasher | stránka, manifest i zapisovaná data ověřené v prohlížeči |
| Skutečná deska | **netestováno** — nemám ji v ruce |
| Rozhovor s Gemini naostro | **netestováno** — nemám API klíč |
| IKEA hub | **netestováno** — z mého prostředí na něj nevidím |

Co přesně je čím podložené a co ověřit nešlo, je v [OVERENI.md](OVERENI.md).

## Instalace

1. Vytvoř si zdarma API klíč na `aistudio.google.com/app/apikey`.
2. Otevři [flasher](https://jenda001224-beep.github.io/Jarvis/) v Chrome nebo Edge na počítači.
3. Vyplň klíč a Wi-Fi (jen 2,4 GHz). Údaje zůstávají v prohlížeči a zapíšou se kabelem do desky.
4. Zapoj desku USB-C kabelem do portu UART (ten, který není uprostřed) a klikni na **Nainstalovat Jarvise**. Při první instalaci zaškrtni Erase device.
5. Displej ukáže hodiny. Řekni „Jarvis“ nebo se ho dotkni.

Změna Wi-Fi nebo klíče: tlačítko **Jen změnit nastavení**. Erase device u toho nezaškrtávej, smazal by se i firmware.

## IKEA

Jarvis si hub DIRIGERA v síti najde sám a nabídne spárování dole na displeji. Dotkni se nabídky a do minuty stiskni tlačítko na spodku hubu. Od té chvíle zná všechna světla a zásuvky jménem i místností.

Bez hubu funguje všechno ostatní, jen světla neumí.

## Ovládání

| Akce | Co udělá |
|---|---|
| „Jarvis“ | začne poslouchat |
| dotyk na displej | začne poslouchat bez oslovení |
| dotyk během rozhovoru | ukončí ho |
| mlčení 10 s po odpovědi | ukončí ho samo |

## Co je dobré vědět

- **Oslovení je anglické „Jarvis“** [džárvis]. Je to hotový model od Espressifu. České „Jarvisi“ to slovo obsahuje, takže by mělo fungovat také, ale vyzkoušené to není.
- **Když Jarvis mluví, neposlouchá.** Mikrofony by slyšely reproduktor. Skočit do řeči mu jde jen dotykem.
- **Dokud běží rozhovor, jde zvuk z pokoje ke Googlu.** Mimo rozhovor ne. V bezplatném tarifu Google data používá ke zlepšování služeb.
- **Bezplatný tarif má limity.** Aktuální čísla ukazuje AI Studio.
- **Hudbu z Apple Music neumí.** K tomu by bylo potřeba zařízení, které ji přehraje do reproduktoru; viz složka `varianta-home-assistant`.
- **Klíč a heslo k Wi-Fi leží v desce nešifrovaně.** Kdo má desku v ruce, může je přečíst.

## Sestavení ze zdrojů

```bash
cd /Volumes/Jan/Jarvis/firmware && source ../tools/idf-env.sh && idf build && cd .. && .tools/venv/bin/python tools/release.py
```

`tools/release.py` zkontroluje hlavičky, rozložení oddílů a přítomnost modelu „Jarvis“ a zapíše `docs/manifest.json` a `docs/firmware/`. Odmítne sestavení, které by při nahrání přepsalo uložené nastavení.

Testy bez desky:

```bash
/Volumes/Jan/Jarvis/firmware/test/run.sh
```

Pustí převzorkování zvuku, stavbu a čtení zpráv pro Gemini a porovnání s oficiální knihovnou `google-genai`.

Zkouška celého rozhovoru proti skutečnému Gemini (potřebuje klíč v `nastaveni.json`, desku ne):

```bash
cd /Volumes/Jan/Jarvis && TMPDIR=.tools/tmp .tools/venv/bin/python firmware/test/live_check.py
```

Nahrání do desky připojené k Macu a výpis z ní:

```bash
cd /Volumes/Jan/Jarvis && source tools/idf-env.sh && python tools/bench.py flash --erase --seconds 60
```

`nastaveni.json` (Wi-Fi a klíč) se do repozitáře neposílá.

## Složky

| Složka | Obsah |
|---|---|
| `firmware/main/` | firmware: `main.c` řízení, `wake.c` oslovení, `live.c` a `proto.c` Gemini, `audio.c` zvuk, `ikea.c` světla, `ui.c` displej |
| `firmware/test/` | testy, které běží na počítači |
| `docs/` | web flasher (GitHub Pages) |
| `tools/` | sestavení a vydání |
| `varianta-home-assistant/` | starší verze postavená na ESPHome a Home Assistantu, umí i Apple Music |
