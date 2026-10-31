# Jak zapnout novinky ve verzi 2.6

Bez těchto kroků funguje všechno ostatní. Každý krok stačí udělat jednou.

## 1. Aplikace (Xcode)

1. Otevři `app/Jarvis.xcodeproj`, cíl Jarvis, záložka Signing & Capabilities.
2. V části iCloud zaškrtni `iCloud.com.jenda.jarvis`. Když tam není, přidej ho přes plus.
3. Spusť na iPhonu a povol upozornění.

## 2. Přístup odkudkoli a upozornění do telefonu

1. Otevři [CloudKit Console](https://icloud.developer.apple.com), kontejner `iCloud.com.jenda.jarvis`,
   Tokens & Keys, Server-to-Server Keys, nový klíč.
2. Vlož tento veřejný klíč:

```
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEj89+zLbEbTQYsj1QLfVNw1w2AseM
OHOXhhMPoyh3Llf78gqq5FztLwQ8a5vENLBQVYTcxWAMgmeOROkAdGVyEg==
-----END PUBLIC KEY-----
```

3. Pošli mi Key ID, nebo spusť:

```bash
python3 /Volumes/Jan/Jarvis/tools/cloud_setup.py --key-id SEM_KEY_ID
```

Displej se restartuje a v aplikaci v části Zařízení uvidíš případnou chybu.

## 3. Zápis do Apple kalendáře

1. Na [account.apple.com](https://account.apple.com) v části Zabezpečení vytvoř heslo pro aplikaci.
2. V aplikaci: Zařízení, Apple kalendář. Zadej Apple ID a to heslo.

## 4. Příchod domů

V aplikaci Zkratky vytvoř dvě automatizace, postup je i v aplikaci pod Zařízení, Příchod domů:

- Wi-Fi R2-D2, Je připojeno, Spustit okamžitě, akce „Přišel jsem domů“ z aplikace Jarvis.
- Wi-Fi R2-D2, Je odpojeno, Spustit okamžitě, akce „Odešel jsem z domova“.

Světla, která se mají rozsvítit, vybereš v Zařízení, Světla a zásuvky. Lampa je už nastavená.

## Co vyzkoušet poslechem

Tohle jsem kvůli tichu zkoušel jen ztlumeně:

- začátek řeči, jestli už nechybí půl sekundy
- „Jarvisi, pusť Radiožurnál“ a jestli tě přes hrající rádio ještě slyší
- „Jarvisi, pusť hnědý šum“
- doběhnutí časovače a budíku
- jestli jas 1 % ve standby stačí, nebo je to už moc
