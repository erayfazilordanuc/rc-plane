// Sayfa JS'ini gercek bir DOM taklidi uzerinde BASTAN SONA calistirir.
// Amac: yukleme aninda atilan bir ReferenceError/TypeError'i yakalamak -
// oyle bir hata o noktadan sonraki butun olay baglayicilarini iptal eder
// ve arayuz sessizce yarim calisir (ornegin "kalibrasyon baslamiyor").
import fs from 'node:fs';
import vm from 'node:vm';

const src = fs.readFileSync(
  new URL('../controller_software/include/web_ui.h', import.meta.url), 'utf8');
const html = /R"HTMLPAGE\(([\s\S]*)\)HTMLPAGE";/.exec(src)[1];
const js   = /<script>([\s\S]*?)<\/script>/.exec(html)[1];
const govde = html.slice(0, html.indexOf('<script>'));

// ---- iskelet DOM ----
const idAttr    = [...govde.matchAll(/id="([^"]+)"/g)].map(m => m[1]);
const dataSurf  = [...govde.matchAll(/data-surf="(\d+)"/g)].map(m => m[1]);
const dataTrim  = [...govde.matchAll(/data-trim="(\w+)"/g)].map(m => m[1]);
const dataMode  = [...govde.matchAll(/data-mode="(\d+)"/g)].map(m => m[1]);

function el(tag = 'div', id = '') {
  const o = {
    tagName: tag.toUpperCase(), id, textContent: '', value: '0', checked: false,
    disabled: false, offsetWidth: 10, style: {}, dataset: {}, _h: {}, _kids: [],
    classList: { _s: new Set(),
      add(c){this._s.add(c);}, remove(c){this._s.delete(c);},
      toggle(c,v){ v===undefined ? (this._s.has(c)?this._s.delete(c):this._s.add(c))
                                 : (v?this._s.add(c):this._s.delete(c)); },
      contains(c){return this._s.has(c);} },
    addEventListener(t,f){ (this._h[t] ||= []).push(f); },
    removeEventListener(){},
    setPointerCapture(){}, releasePointerCapture(){},
    _attr:{}, setAttribute(k,v){ this._attr[k]=v; }, getAttribute(k){ return this._attr[k] ?? null; },
    focus(){}, blur(){}, click(){},
    getBoundingClientRect(){ return {left:0,top:0,width:200,height:200}; },
    querySelector(sel){ return this._q(sel); },
    querySelectorAll(sel){ const r = this._q(sel); return r ? [r] : []; },
    appendChild(c){ this._kids.push(c); c._pn = this; return c; },
    fire(t,e){ (this._h[t]||[]).forEach(f => f(e)); },
    _q(sel){
      const anahtar = sel.replace(/^[.#]/,'').replace(/\[.*/,'');
      return (this._kids.find(k => k.id === anahtar || k.classList.contains(anahtar))
              || el('div'));
    },
  };
  // parentNode gercek DOM'da HER ZAMAN var. Sahte ogede olmadigi icin
  // $('#x').parentNode.style ifadesi undefined'a carpiyor ve kosum sayfayi
  // "bozuk" bildiriyordu - oysa tarayicida sorun yok. Yalan soyleyen bir
  // kosum, hic kosum olmamasindan kotu: gercek bir yukleme hatasini da
  // bu gurultunun icinde kaybediyor.
  //
  // Tembel olusturuluyor: el() icinde dogrudan el() cagirmak sonsuz
  // ozyineleme olurdu.
  Object.defineProperty(o, 'parentNode', {
    get(){ return (o._pn ||= el('div')); },
    set(v){ o._pn = v; },
  });
  return o;
}

const kayit = new Map();
function bul(sel){
  if (kayit.has(sel)) return kayit.get(sel);
  const e = el('div', sel.replace(/^#/, ''));
  // Basili-tut butonlari icin <i> dolgusu
  e.appendChild(Object.assign(el('i'), {id:'i'}));
  e.appendChild(Object.assign(el('b'), {id:'b'}));
  // Kalibrasyon kartlarindaki alt ogeler
  ['mn','md','mx','rev','live','vmn','vmd','vmx','tmn','tmd','tmx','tsw'].forEach(c => {
    const k = el('div'); k.classList.add(c); e.appendChild(k);
  });
  kayit.set(sel, e);
  return e;
}

const belge = {
  documentElement: el('html'),
  hidden: false, fullscreenElement: null,
  _h: {},
  addEventListener(t,f){ (this._h[t] ||= []).push(f); },
  removeEventListener(){},
  querySelector(sel){
    if (sel.startsWith('#')) {
      const kimlik = sel.slice(1).replace(/\s.*/,'');
      if (!idAttr.includes(kimlik) && !sel.includes(' ')) return null;   // GERCEKTEN yok
    }
    return bul(sel);
  },
  querySelectorAll(sel){
    if (sel.includes('[data-surf]')) return dataSurf.map(s => {
      const c = bul('#surf' + s); c.dataset.surf = s; return c;
    });
    if (sel.includes('[data-trim]')) return dataTrim.map(s => {
      const c = bul('#trim' + s); c.dataset.trim = s; c.dataset.d = '5'; return c;
    });
    if (sel.includes('[data-mode]')) return dataMode.map(s => {
      const c = bul('#mode' + s); c.dataset.mode = s; return c;
    });
    return [bul(sel)];
  },
  createElement: el,
};

let hata = null;
const kum = {
  document: belge,
  window: { addEventListener(t,f){ (this._h ||= {})[t] = f; }, location:{hostname:'192.168.4.1', href:'/'} },
  location: { hostname: '192.168.4.1', href: '/' },
  history: { pushState(){}, replaceState(){} },
  navigator: { vibrate(){}, getGamepads(){ return []; }, wakeLock:{ request(){ return Promise.resolve({addEventListener(){}}); } } },
  screen: { orientation: { lock(){ return Promise.reject(new Error('x')); } } },
  performance: { now: () => Date.now() },
  requestAnimationFrame: () => 0,
  cancelAnimationFrame: () => {},
  setInterval: () => 0, clearInterval: () => {},
  setTimeout: () => 0, clearTimeout: () => {},
  fetch: () => Promise.resolve({ json: () => Promise.resolve({}) }),
  WebSocket: function(){ this.readyState = 0; this.send = () => {}; this.close = () => {}; },
  AbortController: function(){ this.signal = {}; this.abort = () => {}; },
  alert: () => {}, confirm: () => true,
  localStorage: { _d:{}, getItem(k){ return this._d[k] ?? null; },
                  setItem(k,v){ this._d[k]=String(v); }, removeItem(k){ delete this._d[k]; } },
  matchMedia: () => ({ matches:false, addEventListener(){}, addListener(){} }),
  addEventListener: () => {},
  Map, Set, Symbol, RegExp, TypeError, Infinity, NaN, undefined,
  console,
  Math, JSON, Date, Object, Array, String, Number, Boolean, Error, Promise, isNaN, parseInt, parseFloat,
};
kum.window.document = belge;
kum.window.matchMedia = kum.matchMedia;
kum.window.location = kum.location;
kum.globalThis = kum;
kum.self = kum;
vm.createContext(kum);

try {
  vm.runInContext(js, kum, { filename: 'web_ui.js' });
  console.log('GECTI  Sayfa JS bastan sona hatasiz calisti.');
} catch (e) {
  hata = e;
  console.log('KALDI  Yukleme sirasinda hata:');
  console.log('  ' + e.constructor.name + ': ' + e.message);
  const satir = /web_ui\.js:(\d+)/.exec(e.stack || '');
  if (satir) {
    const l = +satir[1];
    console.log('  JS satiri ' + l + ':');
    js.split('\n').slice(Math.max(0,l-3), l+2).forEach((t,i) =>
      console.log('   ' + (l-2+i) + ' | ' + t));
  }
}
process.exit(hata ? 1 : 0);
