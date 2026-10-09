const fields = {
  key: document.getElementById("key"),
  ssid: document.getElementById("ssid"),
  password: document.getElementById("password"),
  voice: document.getElementById("voice"),
  place: document.getElementById("place"),
  volume: document.getElementById("volume"),
  search: document.getElementById("search"),
};
const installButton = document.getElementById("install");
const reconfigureButton = document.getElementById("reconfigure");
const missing = document.getElementById("missing");
const meta = document.getElementById("meta");
const volumeValue = document.getElementById("volume-value");

let published = null;
let objectUrls = [];

function settings() {
  return {
    key: fields.key.value.trim(),
    ssid: fields.ssid.value,
    password: fields.password.value,
    voice: fields.voice.value,
    place: fields.place.value.trim() || "Praha",
    volume: Number(fields.volume.value),
    search: fields.search.checked,
  };
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
  const complete = Boolean(published && image && values.key && values.ssid);
  missing.hidden = complete;
  installButton.hidden = !complete;
  reconfigureButton.hidden = !complete;
  if (!published) {
    missing.textContent = "Firmware se nepodařilo načíst.";
    return;
  }
  if (!complete) {
    missing.textContent = image ? "Nejdřív vyplň klíč a Wi-Fi." : "Nastavení je moc dlouhé.";
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
