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
│  · připomínky            │ ─────────────────────────▶ │ IKEA DIRIGERA    │
│                          │   stav tisku               ├──────────────────┤
│                          │ ◀───────────────────────── │ Bambu Lab A1     │
│                          │   (místní síť)             └──────────────────┘
│                          │   události (odkaz iCal)    ┌──────────────────┐
│                          │ ◀───────────────────────── │ kalendář         │
└──────────────────────────┘                            └──────────────────┘
```

1. Slovo „Jarvis“ pozná deska sama, bez internetu. Do té doby nic nikam neposílá.
2. Po probuzení otevře spojení na Gemini Live a posílá zvuk z mikrofonu. Co jsi stihl říct během připojování, se neztratí.
3. Gemini odpovídá rovnou hlasem. Na displeji běží přepis toho, co slyšel a co říká.
4. Když po něm chceš světlo, pošle zpátky příkaz a deska ho předá IKEA hubu.
5. Po odpovědi ještě deset vteřin poslouchá, jestli nechceš pokračovat. Pak spojení zavře.
6. Stav tiskárny si deska drží sama z místní sítě a kalendář si každých deset minut stáhne. Gemini se na ně ptá stejně jako na světla.
7. Když nastane čas připomínky, otevře deska spojení sama a Jarvis promluví první.

## Stav

Verze 2.4.4, ověřeno na skutečné desce 18. 10. 2026.

| Co | Stav |
|---|---|
| Start, displej, dotyk, zvukové čipy, Wi-Fi, nalezení IKEA hubu | běží na desce |
| Oslovení | na „Jarvis“ reaguje spolehlivě; na plynulé „Jarvisi“ při střední citlivosti ve 3 ze 4 pokusů a bez planého poplachu během minuty řeči. Zkoušeno syntetickým hlasem z počítače, ne živým člověkem |
| Rozhovor s Gemini | běží na desce: dotaz hlasem i psaný přes servisní konzoli, odpověď hlasem, bezplatný tarif |
| Plynulost řeči | po úpravě sítě 0 až 1 výpadek na odpověď (měřeno na desce se ztlumeným reproduktorem); poslechem **nepotvrzeno** |
| Počasí, připomínky, samostatné ohlášení a jeho opakování, potvrzení dotykem | běží na desce (zkoušeno potichu přes konzoli) |
| Animace | 29 snímků za sekundu; hodiny v klidu 9 až 10 ms na snímek, domovská obrazovka 2 ms, při řeči kolem 25 ms. Vzhled jsem viděl jen v náhledu na počítači |
| Domovská obrazovka, nastavení, otočení displeje | běží na desce (ovládáno dotyky přes konzoli, všechna čtyři otočení bez pádu); na displeji **neviděno**, skutečný dotyk prstem **nezkoušen** |
| Tiskárna Bambu Lab | čtení stavu otestované na počítači; proti skutečné tiskárně **netestováno** (chybí přístupový kód) |
| Kalendář a doba cesty | výpočty porovnané s referenční knihovnou na počítači; na desce **netestováno** (chybí odkaz na kalendář) |
| IKEA světla | hub se najde; spárování a ovládání **netestováno** |

Co přesně je čím podložené a co ověřit nešlo, je v [OVERENI.md](OVERENI.md).

## Instalace

1. Vytvoř si zdarma API klíč na `aistudio.google.com/app/apikey`. Musí být v projektu na bezplatném tarifu, nebo v placeném s kreditem; projekt s vyčerpaným předplaceným kreditem Gemini odmítne.
2. Otevři [flasher](https://jenda001224-beep.github.io/Jarvis/) v Chrome nebo Edge na počítači.
3. Vyplň klíč a Wi-Fi (jen 2,4 GHz). Tiskárna, kalendář a adresa domova jsou nepovinné. Údaje zůstávají v prohlížeči a zapíšou se kabelem do desky.
4. Zapoj desku USB-C kabelem do portu UART (ten, který není uprostřed) a klikni na **Nainstalovat Jarvise**. Při první instalaci zaškrtni Erase device.
5. Displej ukáže hodiny. Řekni „Jarvisi“ nebo se ho dotkni.

Změna Wi-Fi nebo klíče: tlačítko **Jen změnit nastavení**. Erase device u toho nezaškrtávej, smazal by se i firmware.

## IKEA

Jarvis si hub DIRIGERA v síti najde sám a nabídne spárování dole na displeji. Dotkni se nabídky a do minuty stiskni tlačítko na spodku hubu. Od té chvíle zná všechna světla a zásuvky jménem i místností.

Bez hubu funguje všechno ostatní, jen světla neumí.

## Tiskárna Bambu Lab

Do flasheru opiš přístupový kód z displeje tiskárny (Nastavení → WLAN → Access Code). Jarvis si tiskárnu v síti najde sám, připojí se k ní po místní síti a jen čte; nic na ní neovládá. Když tiskne, ukazuje vpravo nahoře na displeji procenta a zbývající čas.

- „Jarvisi, jak je na tom tisk?“ — procenta, zbývající čas, vrstva
- „Jarvisi, proč se tiskárna zastavila?“ — důvod pauzy a chybová hlášení; jejich popis si deska stáhne od Bambu Lab anglicky a Jarvis ho řekne česky

Když se tiskárna sama nenajde, vyplň ve flasheru i její IP adresu a sériové číslo.

## Kalendář a cesta

Jarvis čte kalendář přes odkaz ve formátu iCal:

1. iPhone: Kalendář → Kalendáře → ⓘ u kalendáře → zapni **Veřejný kalendář** → Sdílet odkaz.
2. Odkaz (začíná `webcal://`) vlož do flasheru. Jdou nejvýš tři kalendáře.

Kdo ten odkaz má, může kalendář číst. Je dlouhý a neuhodnutelný, ale nikam ho nedávej. Stejně funguje tajná adresa iCal z Google Kalendáře.

Když vyplníš i adresu domova, spočítá Jarvis k událostem s místem dobu cesty (koloběžka, kolo, pěšky, auto) a nabídne, že řekne, kdy vyrazit. Adresa domova a místo události se kvůli tomu posílají službám OpenStreetMap (Nominatim a routing.openstreetmap.de). Čas na koloběžce je odhad z délky cyklotrasy při 17 km/h.

Omezení: zná 14 dní dopředu, časová pásma jiná než české bere jako místní čas a z pravidel opakování umí běžné případy (denně, týdně, měsíčně, ročně, výjimky, přesunuté výskyty).

## Domovská obrazovka

Dotyk na hodiny otevře domovskou obrazovku ve stylu Apple Home: nahoře čas, datum a počasí, pod tím řádek se stavem (rozsvícená světla, tisk, další událost, připomínky) a dlaždice světel a zásuvek z IKEA hubu podle místností.

- **Klepnutí na dlaždici** přepne zapnuto/vypnuto.
- **Podržení dlaždice** otevře ovládání jasu, odstínu bílé a barvy.
- **Koule dole uprostřed** začne poslouchat bez oslovení.
- **Lampa vlevo dole** rozsvítí displej bíle.
- **Ozubené kolo vpravo dole** otevře nastavení: hlasitost, jas displeje, za jak dlouho usnout (30 s až nikdy), co ukázat po usnutí (ztlumené hodiny nebo tma), otočení displeje po 90°, citlivost na oslovení a informace o zařízení. Všechno se pamatuje i po vypnutí.

Po uplynutí doby uspání se deska vrátí na hodiny se ztlumeným jasem, nebo úplně zhasne. Probudí ji dotyk nebo oslovení.

## Aplikace pro iPhone

Složka `app/` je aplikace Jarvis pro iPhone (SwiftUI, iOS 17 a novější). Otevři `app/Jarvis.xcodeproj` v Xcode,
vyber svůj iPhone a spusť. Aplikace mluví s displejem přímo po domácí Wi-Fi, bez cloudu, takže funguje jen doma.

- **Spárování:** aplikace displej najde sama (nebo zadej `jarvis.local` či IP). Displej ukáže šestimístný kód,
  ten se opíše do aplikace. Displej si pamatuje čtyři telefony.
- **Úkoly:** společný seznam pro aplikaci i Jarvise („Jarvisi, zapiš si koupit mléko“, „co mám na seznamu?“).
- **Budíky:** opakované ve vybrané dny nebo jednorázové. Displej zvoní, dotyk nebo oslovení budík vypne a Jarvis
  pak pozdraví a řekne, co pána čeká. Připomínky od Jarvise jsou vidět a jdou zrušit.
- **Zařízení (J-WALL Display):** stav, lampička, vzkaz, který Jarvis doma přečte nahlas, a všechna nastavení:
  hlasitost, jas, uspávání, noční režim, orientace, citlivost, hlas, místo, hledání, domov a cesta, Wi-Fi, restart.

`app/Tools/generate_project.py` znovu vytvoří projekt, když přibude soubor.

## Noční režim

Od 23:00 do 7:00 displej po půl minutě klidu úplně zhasne, místo hodin je tma. Dotyk nebo oslovení ho probudí.
Čas a zapnutí jsou v nastavení na displeji i v aplikaci.

## Připomínky

„Jarvisi, připomeň mi v pět, ať vypnu troubu“ nebo odpověď „ano“ na nabídku, že dá vědět, kdy vyrazit. Připomínky přežijí vypnutí desky, vejde se jich osm.

Když nastane čas, Jarvis promluví sám a zeptá se, jestli ho slyšíš. Stačí cokoli odpovědět nebo se dotknout displeje. Když se nikdo neozve, zkusí to po minutě a půl znovu, celkem čtyřikrát. Potom nechá dole na displeji „Připomínka čeká“ a řekne ji hned, jak ho příště oslovíš.

## Ovládání

| Akce | Co udělá |
|---|---|
| „Jarvisi“ | odpoví „Ano, pane?“, „Poslouchám, pane.“ nebo „Prosím, pane?“ a poslouchá; když mluvíš rovnou dál, nepřerušuje tě |
| „Jarvisi, v kolik dnes zavírá bazén?“ | hledá na webu (s klíčem Tavily nebo placeným tarifem Gemini) |
| „Jarvisi, dej mi trošku světla“ | rozsvítí displej bíle na plný jas; dotyk nebo „zhasni displej“ ho vrátí |
| „Jarvisi, zapni LEDky, modrá, jas 100“ | zapne světla z IKEA hubu a nastaví barvu a jas |
| „Jarvisi, jak je na tom tisk?“ | stav tiskárny Bambu Lab |
| „Jarvisi, co dneska budu dělat?“ | dnešní události z kalendáře, u těch s místem i doba cesty |
| „Jarvisi, připomeň mi za dvacet minut…“ | nastaví připomínku |
| „Jarvisi, jak bude zítra?“ | předpověď počasí ze služby Open-Meteo |
| dotyk na hodiny | otevře domovskou obrazovku |
| koule na domovské obrazovce | začne poslouchat bez oslovení |
| dotyk během rozhovoru | ukončí ho |
| mlčení 10 s po odpovědi | ukončí ho samo |

## Co je dobré vědět

- **Jarvis vyká a říká „pane“.** Mluví jako Jarvis z Iron Mana, se suchým humorem. Povahu má v `firmware/main/main.c` v textu `PERSONA`.
- **Oslovení „Jarvisi“ hlídá model natrénovaný na „Jarvis“.** Je to hotový model od Espressifu. Samotné „Jarvis“ pozná vždy, plynulé „Jarvisi“ jen se zvýšenou citlivostí, proto je výchozí citlivost střední. Když se ozývá sám, sniž ji ve flasheru; když neslyší, řekni „Jarvis“ a po krátké pauze pokračuj.
- **Bezplatný tarif Gemini stačí,** jen nesmí hledat na Googlu (ověřeno 18. 10. 2026: Google odmítá hledání v živém
  hovoru i v obyčejném dotazu). Na webu proto Jarvis hledá přes službu Tavily: ve flasheru vyplň její klíč
  (účet zdarma bez karty, 1 000 hledání měsíčně). Kdo zapne placený tarif Gemini, tomu Jarvis hledá rovnou na Googlu;
  jednou denně to zkusí sám. Počasí bere z Open-Meteo.
- **Hlasitost 0 znamená ticho:** reproduktor i zesilovač zůstanou vypnuté, všechno ostatní běží dál.
- **Deska chce slušnou Wi-Fi.** Přenáší zvuk oběma směry naživo; se slabým signálem (pod −70 dBm) se řeč trhá. Sílu signálu vypisuje do servisního výpisu.
- **Když Jarvis mluví, neposlouchá.** Mikrofony by slyšely reproduktor. Skočit do řeči mu jde jen dotykem.
- **Dokud běží rozhovor, jde zvuk z pokoje ke Googlu.** Mimo rozhovor ne. V bezplatném tarifu Google data používá ke zlepšování služeb.
- **Bezplatný tarif má limity.** Aktuální čísla ukazuje AI Studio.
- **Hudbu neumí.** „Jarvisi, pusť Bad Times“ zatím skončí omluvou. Bylo by k tomu potřeba zařízení, které skladbu najde a přehraje do reproduktoru, nejspíš Raspberry, které už hraje přes AirPlay do Bluetooth.
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

Pustí převzorkování zvuku, stavbu a čtení zpráv pro Gemini, porovnání s oficiální knihovnou `google-genai`, čtení stavu tiskárny, připomínky a porovnání kalendáře s knihovnou `recurring_ical_events`.
Python k tomu potřebuje balíčky `google-genai`, `websockets`, `icalendar`, `recurring-ical-events` a `python-dateutil`.

Náhled displeje bez desky (stejný kód, jaký kreslí na desce):

```bash
cd /Volumes/Jan/Jarvis && mkdir -p .tools/tmp/fx && cc -O2 -Ifirmware/main -o .tools/tmp/fx/fx_preview firmware/test/fx_preview.c firmware/main/fx.c firmware/main/scene.c -lm && .tools/tmp/fx/fx_preview .tools/tmp/fx && .tools/venv/bin/python firmware/test/compose.py .tools/tmp/fx
```

Zkouška celého rozhovoru proti skutečnému Gemini (potřebuje klíč v `nastaveni.json`, desku ne):

```bash
cd /Volumes/Jan/Jarvis && TMPDIR=.tools/tmp .tools/venv/bin/python firmware/test/live_check.py
```

Servisní konzole: deska čte příkazy z USB. `ask TEXT` položí psaný dotaz, `mute on|off` ztlumí reproduktor, `dry on|off` oslovení jen zapisuje a nezačne rozhovor, `touch` nahradí dotyk, `remind VTEŘINY TEXT` nastaví zkušební připomínku, `tap X Y` a `hold X Y` klepnou nebo podrží prst na souřadnici displeje, `turn 0–3` otočí displej po 90°. Dotaz bez mluvení a s výpisem:

```bash
cd /Volumes/Jan/Jarvis && source tools/idf-env.sh && python tools/bench.py log --seconds 40 --send "12:ask Kolik je hodin?"
```

Změna jednoho uloženého nastavení bez přepsání ostatních (hodnoty se nevypisují):

```bash
cd /Volumes/Jan/Jarvis && source tools/idf-env.sh && python tools/bench.py set volume=70
```

Nahrání do desky připojené k Macu a výpis z ní:

```bash
cd /Volumes/Jan/Jarvis && source tools/idf-env.sh && python tools/bench.py flash --erase --seconds 60
```

`nastaveni.json` (Wi-Fi a klíč) se do repozitáře neposílá.

## Složky

| Složka | Obsah |
|---|---|
| `firmware/main/` | firmware: `main.c` řízení a osobnost, `wake.c` oslovení, `live.c` a `proto.c` Gemini, `audio.c` zvuk, `ikea.c` světla, `printer.c` a `bambu.c` tiskárna, `calendar.c` a `agenda.c` kalendář, `travel.c` a `geo.c` doba cesty, `reminders.c` připomínky, `weather.c` a `meteo.c` počasí, `console.c` servisní konzole, `web.c` stahování, `search.c` a `tavily.c` hledání na webu, `sounds/` nahraná odpověď na oslovení, `ui.c` stránky a přechody mezi nimi, `home.c` domovská obrazovka a nastavení, `control.c` ovládání světel z dotyku, `fx.c` a `scene.c` kreslení a animace koule a prstenců |
| `firmware/test/` | testy, které běží na počítači |
| `docs/` | web flasher (GitHub Pages) |
| `tools/` | sestavení, vydání a zkouška rozhraní pro aplikaci (`api_check.py`) |
| `app/` | aplikace pro iPhone |
| `varianta-home-assistant/` | starší verze postavená na ESPHome a Home Assistantu, umí i Apple Music |
