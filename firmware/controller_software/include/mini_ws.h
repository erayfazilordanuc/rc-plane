// ============================================================
// mini_ws.h - Tek istemcili, kutuphanesiz WebSocket sunucusu (RFC 6455)
//
// NEDEN VAR:
// Kumanda sayfasi kol konumlarini 20 Hz gonderiyor. ESP32'nin WebServer'i
// her cevaba "Connection: close" koyuyor ve tek istemciyi seri isliyor;
// yani her komut icin yeni bir TCP el sikismasi gerekiyor. 10 Hz'de bu
// calisiyor ama gecikme dalgalaniyor - kolu hizli hareket ettirdiginde
// yuzeyler tokmakli takip ediyor. WebSocket'te baglanti bir kez kuruluyor,
// her komut 2 byte baslikla gidiyor ve gecikme tek haneli ms'ye iniyor.
//
// NEDEN KUTUPHANE DEGIL:
// Ucusa hazir bir firmware'in derlemesi internete bagli olmamali. Ihtiyac
// duyulan alt kume kucuk (tek istemci, kisa metin cerceveleri), bu yuzden
// SHA-1 ve base64 dahil her sey burada. Disaridan tek satir gelmiyor.
//
// KAPSAM ve SINIRLAR (bilerek):
//   - AYNI ANDA TEK istemci. Ikincisi baglanirsa birincinin yerini alir;
//     kumandaya zaten tek cihaz baglanmasi gerekiyor (AP_MAX_CONN = 1).
//   - Sadece metin (0x1) ve kontrol cerceveleri. Ikili cerceve reddedilir.
//   - Parcali (fragmented) cerceve desteklenmiyor; kisa komutlar hicbir
//     zaman parcalanmaz.
//   - En buyuk cerceve KAPASITE kadar. Fazlasi baglantiyi kapatir.
//   - TLS yok. Kumandanin kendi AP'si, WPA2 ile sifreli; sayfa internete
//     acik degil.
//
// EL SIKISMA BLOKLAMAZ: header'lar loop() turlari boyunca birikir. Ucus
// sirasinda tarayici yeniden baglanirsa kontrol dongusu duraklamaz.
// ============================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ctype.h>
#include <string.h>
#include <lwip/sockets.h>   // select() - bloklamayan yazma yoklamasi icin

// ------------------------------------------------------------
// SHA-1 (RFC 3174) - sadece el sikismasindaki Sec-WebSocket-Accept icin.
// ------------------------------------------------------------
struct MiniSha1 {
  uint32_t h[5];
  uint8_t  blok[64];
  uint32_t blokLen;
  uint64_t toplamBit;

  void basla() {
    h[0] = 0x67452301; h[1] = 0xEFCDAB89; h[2] = 0x98BADCFE;
    h[3] = 0x10325476; h[4] = 0xC3D2E1F0;
    blokLen = 0; toplamBit = 0;
  }

  static uint32_t rotl(uint32_t v, uint32_t n) { return (v << n) | (v >> (32 - n)); }

  void blokIsle(const uint8_t* p) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
      w[i] = ((uint32_t)p[i * 4] << 24) | ((uint32_t)p[i * 4 + 1] << 16) |
             ((uint32_t)p[i * 4 + 2] << 8) | (uint32_t)p[i * 4 + 3];
    }
    for (int i = 16; i < 80; i++) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if      (i < 20) { f = (b & c) | ((~b) & d);          k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
      else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
      const uint32_t t = rotl(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rotl(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }

  void ekle(const uint8_t* veri, size_t n) {
    toplamBit += (uint64_t)n * 8;
    while (n--) {
      blok[blokLen++] = *veri++;
      if (blokLen == 64) { blokIsle(blok); blokLen = 0; }
    }
  }

  void bitir(uint8_t ozet[20]) {
    blok[blokLen++] = 0x80;
    if (blokLen > 56) {
      while (blokLen < 64) blok[blokLen++] = 0;
      blokIsle(blok); blokLen = 0;
    }
    while (blokLen < 56) blok[blokLen++] = 0;
    for (int i = 7; i >= 0; i--) blok[blokLen++] = (uint8_t)(toplamBit >> (i * 8));
    blokIsle(blok);
    for (int i = 0; i < 5; i++) {
      ozet[i * 4]     = (uint8_t)(h[i] >> 24);
      ozet[i * 4 + 1] = (uint8_t)(h[i] >> 16);
      ozet[i * 4 + 2] = (uint8_t)(h[i] >> 8);
      ozet[i * 4 + 3] = (uint8_t)(h[i]);
    }
  }
};

// base64 kodlayici (20 baytlik ozet icin - cikis 28 karakter + '\0')
static inline void miniB64(const uint8_t* veri, size_t n, char* cikis) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  size_t o = 0;
  for (size_t i = 0; i < n; i += 3) {
    const uint32_t a = veri[i];
    const uint32_t b = (i + 1 < n) ? veri[i + 1] : 0;
    const uint32_t c = (i + 2 < n) ? veri[i + 2] : 0;
    const uint32_t v = (a << 16) | (b << 8) | c;
    cikis[o++] = T[(v >> 18) & 63];
    cikis[o++] = T[(v >> 12) & 63];
    cikis[o++] = (i + 1 < n) ? T[(v >> 6) & 63] : '=';
    cikis[o++] = (i + 2 < n) ? T[v & 63]        : '=';
  }
  cikis[o] = 0;
}

// ------------------------------------------------------------
class MiniWebSocket {
 public:
  // Gelen metin mesaji geri cagrisi. Metin '\0' ile sonlandirilmis olarak
  // verilir; tampon cagri bitince gecersizlesir, saklamak istersen kopyala.
  typedef void (*MesajFn)(const char* metin, size_t uzunluk);

  explicit MiniWebSocket(uint16_t port) : _sunucu(port) {}

  void basla(MesajFn cb) { _cb = cb; _sunucu.begin(); _sunucu.setNoDelay(true); }

  bool bagli() const { return _durum == ACIK; }

  // Bagli istemciden en son ne zaman veri geldi. Ikinci istemciyi
  // reddederken "mevcut istemci hala yasiyor mu" sorusunun cevabi bu.
  uint32_t sonVeriMs() const { return _sonVeriMs; }

 private:
  uint32_t _sonVeriMs  = 0;  // son istemci verisi (tek istemci kurali icin)
  uint32_t _sonRedMs   = 0;  // son red logu
  uint32_t _sonDevirMs = 0;  // son devretme (ping-pong'u engellemek icin)

 public:

  // loop()'ta her turda cagrilmali. Hicbir asamada beklemez.
  void dongu() {
    _kabulEt();
    if (_durum == BOSTA) return;

    if (!_istemci.connected()) { _kapat("baglanti dustu"); return; }

    // Gelen baytlari tampona al. Tampon KAPASITE'ye kadar doldurulur;
    // dizinin sonundaki fazla baytlar bilerek bos birakiliyor ki metin
    // cercevesini gecici olarak sifir bayti ile sonlandirmak tasma yaratmasin.
    while (_istemci.available() && _len < KAPASITE) {
      _tampon[_len++] = (uint8_t)_istemci.read();
      _sonVeriMs = millis();
    }

    if (_durum == ELSIKISMA) _elSikismaIlerle();
    if (_durum == ACIK)      _cerceveleriIsle();

    // Tampon dolduysa ve hala cozulemiyorsa protokol bozulmustur
    if (_len >= KAPASITE) _kapat("tampon tasti");
  }

  // Metin cerceve gonderir. Baglanti yoksa ya da soket o an yazmaya hazir
  // degilse sessizce dusurulur - kontrol dongusu WebSocket'in durumuna
  // bagimli olmamali.
  //
  // NEDEN "hazir mi" diye soruyoruz: WiFiClient::write() soket tikandiginda
  // select'i 1 saniyelik zaman asimiyla 10 kez deniyor, yani en kotu halde
  // ~10 SANIYE blokluyor. Bunu ucus dongusunde yasamak kabul edilemez -
  // o sure boyunca ne telsiz paketi cikar ne failsafe sayaci islenir.
  // Onceden select ile yoklayip hazir degilse pas geciyoruz: durum mesajlari
  // zaten idempotent, birini atlamanin maliyeti 100 ms gecikmeden ibaret.
  bool gonder(const char* metin) {
    if (_durum != ACIK || !_istemci.connected()) return false;
    if (!_yazilabilir()) return false;
    const size_t n = strlen(metin);
    if (n > KAPASITE) return false;   // bu sunucu bu kadar buyuk mesaj gondermiyor

    // Baslik ve yuk TEK write ile gider. Ayri yazilsalar baslik ile govde
    // farkli TCP segmentlerine dagilir (setNoDelay acik); ikinci yazma
    // yarim kalirsa akis senkronu bozulur ve istemci cerceveyi cozemez.
    uint8_t cerceve[KAPASITE + 4];
    size_t basN;
    if (n < 126) {
      cerceve[0] = 0x81; cerceve[1] = (uint8_t)n; basN = 2;
    } else {
      cerceve[0] = 0x81; cerceve[1] = 126;
      cerceve[2] = (uint8_t)(n >> 8); cerceve[3] = (uint8_t)n; basN = 4;
    }
    memcpy(cerceve + basN, metin, n);

    if (_istemci.write(cerceve, basN + n) != basN + n) {
      _kapat("yazma hatasi");
      return false;
    }
    return true;
  }

 private:
  enum Durum { BOSTA, ELSIKISMA, ACIK };
  static const uint16_t KAPASITE = 600;   // kabul edilen en buyuk cerceve

  WiFiServer _sunucu;
  WiFiClient _istemci;
  Durum      _durum = BOSTA;
  MesajFn    _cb    = nullptr;
  uint8_t    _tampon[KAPASITE + 8];   // +8: sonlandirici icin emniyet payi
  uint16_t   _len   = 0;
  uint32_t   _basladiMs = 0;

  // Soket su an bloke etmeden yaziyi kabul eder mi? Zaman asimi sifir olan
  // bir select ile sorulur; cevap alinamiyorsa "hazir degil" kabul edilir
  // (guvenli taraf: yazmayi hic denemeyip donguyu serbest birakmak).
  bool _yazilabilir() {
    const int s = _istemci.fd();
    if (s < 0) return false;
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);
    struct timeval tv = {0, 0};
    return select(s + 1, nullptr, &set, nullptr, &tv) > 0 && FD_ISSET(s, &set);
  }

  void _kapat(const char* sebep) {
    if (_durum != BOSTA) Serial.printf("[WS] Baglanti kapandi: %s\n", sebep);
    _istemci.stop();
    _durum = BOSTA;
    _len   = 0;
  }

  // ---- SOKET "ILK GELENE" DEGIL "CANLI OLANA" VERILIR ----
  //
  // Kural bir ara "ilk gelen kazanir" + 3 sn sessizlik kacisi seklindeydi ve
  // OLCUMDE TERS TEPTI: telefonda iki sekme acikken soket ILK baglanan
  // sekmede kaliyor, ama o sekme arka planda ve tarayici onu kisitladigi
  // icin HIC komut gondermiyor. Pilotun kullandigi canli sekme ise surekli
  // reddediliyor. Logdaki imza:
  //     [WS] Ikinci istemci REDDEDILDI        (canli sekme reddediliyor)
  //     [WEB] Arayuz sustu (2001 ms)          (olu sekme komut gondermiyor)
  //     [WS] eski istemci sessiz - devredildi (3 sn sonra nihayet)
  // Sonuc: komut akisi kesiliyor, gaz ve yuzeyler notre gidip donuyor -
  // yani kuralin onlemesi gereken git-gel'i kuralin kendisi uretiyor.
  //
  // Dogru olcut GELIS SIRASI degil CANLILIK: arayuz 20 Hz komut gonderiyor,
  // yani yasayan bir istemci 500 ms sessiz KALAMAZ. Kisitlanmis/arka plana
  // atilmis bir sekme ise kalir. Bu esik, soketi pilotun gercekten
  // dokundugu sekmeye veriyor.
  static const uint32_t ISTEMCI_OLU_MS = 500;

  // Devretme sonrasi bu sure boyunca yeniden devretmiyoruz. Iki sekme de
  // olu oldugunda saniyede birkac kez birbirine devredip durmasini
  // (eski "atma savasi") engelleyen tek sart bu.
  static const uint32_t DEVIR_BEKLE_MS = 1500;

  void _kabulEt() {
    WiFiClient yeni = _sunucu.available();
    if (!yeni) {
      // El sikisma yarida kaldiysa asili birakma
      if (_durum == ELSIKISMA && millis() - _basladiMs > 3000) _kapat("el sikisma zaman asimi");
      return;
    }
    // ---- TEK ISTEMCI: ILK GELEN KAZANIR ----
    //
    // Eskiden yeni gelen eskisini atiyordu ("son gelen kazanir"). Ikinci bir
    // sekme ya da PWA acik kaldiginda iki taraf birbirini atip duruyor.
    // Olculdu: saniyede ~148 "yeni istemci geldi" cifti, kumandanin loop'u
    // boguldu, telsiz servisi ac kaldi ve link dustu.
    //
    // Ama KOSULSUZ reddetmek de olmaz: sayfa yenilendiginde ya da telefonun
    // WiFi'i dustugunde eski soket TCP seviyesinde hemen kapanmayabilir ve
    // pilot kendi arayuzunden kilitlenir. Kacis kapisi: mevcut istemci
    // ISTEMCI_OLU_MS boyunca hic veri gondermediyse olu kabul edilir ve
    // yerini yenisine birakir.
    if (_durum != BOSTA) {
      const bool canli  = (millis() - _sonVeriMs < ISTEMCI_OLU_MS);
      const bool bekler = (_sonDevirMs && millis() - _sonDevirMs < DEVIR_BEKLE_MS);
      if (canli || bekler) {
        yeni.stop();                       // aktif arayuz var -> ikinciyi REDDET
        if (millis() - _sonRedMs > 2000) { // log akip gitmesin
          _sonRedMs = millis();
          Serial.printf("[WS] Ikinci istemci REDDEDILDI (%s).\n",
                        canli ? "bagli arayuz komut gonderiyor"
                              : "devretme beklemesi");
        }
        return;
      }
      _sonDevirMs = millis();
      _kapat("bagli arayuz sessiz - canli istemciye devredildi");
    }
    _istemci   = yeni;
    _istemci.setNoDelay(true);   // Nagle kapali: 8 baytlik komut hemen ciksin
    _durum     = ELSIKISMA;
    _len       = 0;
    _basladiMs = millis();
    _sonVeriMs = millis();
  }

  // HTTP yukseltme istegini bekler. Basliklarin tamami gelene kadar
  // (bos satir) hicbir sey yapmadan doner - loop bloke olmaz.
  void _elSikismaIlerle() {
    if (_len < 4) return;
    _tampon[_len] = 0;
    const char* son = strstr((const char*)_tampon, "\r\n\r\n");
    if (!son) return;

    // Sec-WebSocket-Key basligini bul (buyuk/kucuk harf duyarsiz)
    const char* k = _basliksizAra((const char*)_tampon, "sec-websocket-key:");
    if (!k) { _kapat("Sec-WebSocket-Key yok"); return; }
    while (*k == ' ') k++;
    char anahtar[80];
    size_t i = 0;
    while (*k && *k != '\r' && *k != '\n' && i < sizeof(anahtar) - 1) anahtar[i++] = *k++;
    anahtar[i] = 0;

    // RFC 6455: accept = base64(sha1(key + GUID))
    static const char GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    MiniSha1 s; s.basla();
    s.ekle((const uint8_t*)anahtar, strlen(anahtar));
    s.ekle((const uint8_t*)GUID, sizeof(GUID) - 1);
    uint8_t ozet[20]; s.bitir(ozet);
    char kabul[32]; miniB64(ozet, 20, kabul);

    char cevap[220];
    const int n = snprintf(cevap, sizeof(cevap),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", kabul);
    _istemci.write((const uint8_t*)cevap, n);

    _len   = 0;      // el sikismadan artan bayt olmaz, istek burada biter
    _durum = ACIK;
    Serial.println("[WS] Istemci baglandi.");
  }

  static const char* _basliksizAra(const char* metin, const char* kucukAnahtar) {
    const size_t an = strlen(kucukAnahtar);
    for (const char* p = metin; *p; p++) {
      size_t i = 0;
      while (i < an && p[i] && (char)tolower((unsigned char)p[i]) == kucukAnahtar[i]) i++;
      if (i == an) return p + an;
    }
    return nullptr;
  }

  // Tamponun basindan cozulebilen tum cerceveleri isler.
  void _cerceveleriIsle() {
    for (;;) {
      if (_len < 2) return;

      const uint8_t opcode = _tampon[0] & 0x0F;
      const bool    maskeli = (_tampon[1] & 0x80) != 0;
      uint32_t      uzunluk = _tampon[1] & 0x7F;
      uint16_t      ofset   = 2;

      if (uzunluk == 126) {
        if (_len < 4) return;
        uzunluk = ((uint32_t)_tampon[2] << 8) | _tampon[3];
        ofset   = 4;
      } else if (uzunluk == 127) {
        _kapat("64-bit cerceve desteklenmiyor");   // bu sunucuya asla gelmemeli
        return;
      }

      // RFC 6455: istemci -> sunucu cerceveleri HER ZAMAN maskeli olmali
      if (!maskeli) { _kapat("maskesiz istemci cercevesi"); return; }
      if (_len < ofset + 4u + uzunluk) return;      // cerceve henuz tam degil

      const uint8_t* maske = &_tampon[ofset];
      uint8_t*       yuk   = &_tampon[ofset + 4];
      for (uint32_t i = 0; i < uzunluk; i++) yuk[i] ^= maske[i & 3];

      const uint16_t cerceveBoyu = (uint16_t)(ofset + 4 + uzunluk);

      if (opcode == 0x8) {                 // close
        _kapat("istemci kapatti");
        return;
      } else if (opcode == 0x9) {          // ping -> pong
        uint8_t bas[2] = { 0x8A, (uint8_t)(uzunluk < 126 ? uzunluk : 0) };
        _istemci.write(bas, 2);
        if (uzunluk && uzunluk < 126) _istemci.write(yuk, uzunluk);
      } else if (opcode == 0x1) {          // metin
        const uint8_t sonrakiBayt = yuk[uzunluk];   // gecici olarak sakla
        yuk[uzunluk] = 0;
        if (_cb) _cb((const char*)yuk, uzunluk);
        // Geri cagri icinden gonder() cagrilabilir ve o da baglantiyi
        // kapatabilir (yazma hatasi). O durumda _len sifirlanmis olur;
        // asagidaki cikarma isaretsiz tasip devasa bir uzunluk uretirdi.
        if (_durum != ACIK) return;
        yuk[uzunluk] = sonrakiBayt;
      }
      // 0x2 (ikili) ve 0xA (pong) sessizce yok sayilir

      // Islenen cerceveyi tampondan cikar
      if (_len < cerceveBoyu) { _len = 0; return; }
      _len = (uint16_t)(_len - cerceveBoyu);
      if (_len) memmove(_tampon, _tampon + cerceveBoyu, _len);
    }
  }
};
