const fields = {
  key: document.getElementById("key"),
  ssid: document.getElementById("ssid"),
  password: document.getElementById("password"),
  voice: document.getElementById("voice"),
  place: document.getElementById("place"),
  volume: document.getElementById("volume"),
  sensitivity: document.getElementById("sensitivity"),
  search: document.getElementById("search"),
  searchKey: document.getElementById("search-key"),
  printerCode: document.getElementById("printer-code"),
  printerIp: document.getElementById("printer-ip"),
  printerSerial: document.getElementById("printer-serial"),
  calendars: document.getElementById("calendars"),
  home: document.getElementById("home"),
  transport: document.getElementById("transport"),
};
const installButton = document.getElementById("install");
const reconfigureButton = document.getElementById("reconfigure");
const missing = document.getElementById("missing");
const meta = document.getElementById("meta");
const volumeValue = document.getElementById("volume-value");

let published = null;
let objectUrls = [];

function calendarLinks() {
  return fields.calendars.value.split(/\s+/).filter(Boolean);
}

function settings() {
  const values = {
    key: fields.key.value.trim(),
    ssid: fields.ssid.value,
    password: fields.password.value,
    voice: fields.voice.value,
    place: fields.place.value.trim() || "Praha",
    volume: Number(fields.volume.value),
    sensitivity: Number(fields.sensitivity.value),
    search: fields.search.checked,
  };
  const optional = {
    printer_code: fields.printerCode.value.trim(),
    printer_ip: fields.printerIp.value.trim(),
    printer_serial: fields.printerSerial.value.trim().toUpperCase(),
    home: fields.home.value.trim(),
    search_key: fields.searchKey.value.trim(),
  };
  Object.entries(optional).forEach(([name, value]) => {
    if (value) {
      values[name] = value;
    }
  });
  if (calendarLinks().length) {
    values.calendars = calendarLinks();
  }
  if (values.home) {
    values.transport = fields.transport.value;
  }
  return values;
}

function problem(values) {
  const links = values.calendars || [];
  if (links.length > 3) {
    return "Kalendáře mohou být nejvýš tři.";
  }
  if (links.some((link) => !/^(webcal|https):\/\/\S+$/i.test(link) || link.length > 255)) {
    return "Odkaz na kalendář má začínat webcal:// nebo https:// a mít nejvýš 255 znaků.";
  }
  if (values.printer_ip && !/^\d{1,3}(\.\d{1,3}){3}$/.test(values.printer_ip)) {
    return "IP adresa tiskárny má tvar 10.0.0.42.";
  }
  if (Boolean(values.printer_ip) !== Boolean(values.printer_serial)) {
    return "IP adresu a sériové číslo tiskárny vyplň buď obě, nebo žádné.";
  }
  if ((values.printer_ip || values.printer_serial) && !values.printer_code) {
    return "K tiskárně chybí přístupový kód.";
  }
  if (values.search_key && !/^tvly-[A-Za-z0-9_-]+$/.test(values.search_key)) {
    return "Klíč Tavily začíná tvly- a nemá mezery.";
  }
  return "";
}

function settingsImage(values, size) {
  const text = new TextEncoder().encode(JSON.stringify(values));
  if (text.length >= size) {
    return null;
  }
  const image = new Uint8Array(size).fill(0xff);
  image.set(text);
  image[text.length] = 0;
  return image;
}

function absolute(path) {
  return new URL(path, location.href).toString();
}

function manifestUrl(parts) {
  const manifest = {
    name: published.name,
    version: published.version,
    new_install_prompt_erase: true,
    new_install_improv_wait_time: 0,
    builds: [{ chipFamily: published.builds[0].chipFamily, parts }],
  };
  const url = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: "application/json" }));
  objectUrls.push(url);
  return url;
}

function refresh() {
  volumeValue.textContent = fields.volume.value;
  objectUrls.forEach((url) => URL.revokeObjectURL(url));
  objectUrls = [];
  const values = settings();
  const image = published ? settingsImage(values, published.config.size) : null;
  const wrong = problem(values);
  const complete = Boolean(published && image && values.key && values.ssid && !wrong);
  missing.hidden = complete;
  installButton.hidden = !complete;
  reconfigureButton.hidden = !complete;
  if (!published) {
    missing.textContent = "Firmware se nepodařilo načíst.";
    return;
  }
  if (!complete) {
    missing.textContent = !image ? "Nastavení je moc dlouhé." : wrong || "Nejdřív vyplň klíč a Wi-Fi.";
    return;
  }
  const imageUrl = URL.createObjectURL(new Blob([image]));
  objectUrls.push(imageUrl);
  const settingsPart = { path: imageUrl, offset: published.config.offset };
  const firmwareParts = published.builds[0].parts.map((part) => ({ path: absolute(part.path), offset: part.offset }));
  installButton.setAttribute("manifest", manifestUrl([...firmwareParts, settingsPart]));
  reconfigureButton.setAttribute("manifest", manifestUrl([settingsPart]));
}

Object.values(fields).forEach((field) => field.addEventListener("input", refresh));

fetch("manifest.json", { cache: "no-store" })
  .then((response) => response.json())
  .then((manifest) => {
    published = manifest;
    const megabytes = (manifest.builds[0].size / 1048576).toFixed(1);
    meta.textContent = `Verze ${manifest.version} · ${megabytes} MB · ${manifest.builds[0].chipFamily}`;
    refresh();
  })
  .catch(() => {
    meta.textContent = "Verzi se nepodařilo načíst";
    refresh();
  });
