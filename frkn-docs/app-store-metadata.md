# App Store Connect — App Information draft

Copy-paste materials for the App Store listing. Primary language: Russian;
add English localization as well (and optionally Ukrainian).

> URL-ы должны реально открываться до ревью: `https://frkn.org`,
> `https://frkn.org/privacy-policy/` (privacy policy — обязателен для VPN, гайд 5.4).

---

## Name / Subtitle

- **Name:** Dopamine by FRKN
- **Subtitle (RU, ≤30):** Быстрый и приватный VPN
- **Subtitle (EN, ≤30):** Fast & private VPN

---

## Promotional Text (≤170 chars, можно менять без ревью)

**EN:**
```
Connect in one tap. Free servers, modern anti-censorship protocols, and full control over your privacy — no registration needed.
```

**RU:**
```
Подключение в один тап. Бесплатные серверы, современные протоколы против блокировок и полный контроль над приватностью.
```

---

## Description

**EN:**
```
Dopamine is a free, open-source VPN app by FRKN.

Connect in one tap — no registration required. Start with a free trial or enter your subscription key and you're online in seconds.

WHY DOPAMINE

• Modern anti-censorship protocols: AmneziaWG, WireGuard, OpenVPN with Cloak, XRay with REALITY — designed to work even where VPNs are blocked
• Free and premium servers around the world
• Split tunneling — choose which apps and sites use the VPN
• KillSwitch — your traffic never leaks if the connection drops
• Import any config: QR code, key, or file. Self-hosted setups welcome
• Open source — the code is public on GitHub

PRIVACY FIRST

No accounts required to try, no ads, no trackers. Your connection is yours.

Subscription options: 1, 3, 6 or 12 months. Payment is charged to your Apple ID at confirmation. The subscription renews automatically unless auto-renew is turned off at least 24 hours before the end of the current period. Manage or cancel anytime in your Apple ID settings.

Terms of Use: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Privacy Policy: https://frkn.org/privacy-policy/
```

**RU:**
```
Dopamine — бесплатный VPN с открытым исходным кодом от FRKN.

Подключение в один тап — без регистрации. Начните с бесплатного триала или введите ключ подписки — и вы в сети за секунды.

ПОЧЕМУ DOPAMINE

• Современные протоколы против блокировок: AmneziaWG, WireGuard, OpenVPN с Cloak, XRay с REALITY — работают даже там, где VPN блокируют
• Бесплатные и премиум-серверы по всему миру
• Раздельное туннелирование — выбирайте, какие приложения и сайты идут через VPN
• KillSwitch — трафик не утечёт, даже если соединение оборвётся
• Импорт любых конфигураций: QR-код, ключ или файл. Поддержка self-hosted серверов
• Открытый исходный код — весь код публичен на GitHub

ПРИВАТНОСТЬ ПРЕЖДЕ ВСЕГО

Не нужен аккаунт, чтобы попробовать. Никакой рекламы и трекеров.

Варианты подписки: 1, 3, 6 или 12 месяцев. Оплата списывается с вашего Apple ID при подтверждении. Подписка продлевается автоматически, если автопродление не отключено минимум за 24 часа до конца периода. Управление и отмена — в настройках Apple ID.

Условия использования: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Политика конфиденциальности: https://frkn.org/privacy-policy/
```

---

## Keywords (≤100 chars, через запятую, без пробелов после запятых)

**EN:**
```
vpn,wireguard,openvpn,proxy,amnezia,privacy,secure,proxy server,free vpn,vpn client
```
(99 chars)

**RU:**
```
vpn,впн,прокси,wireguard,openvpn,приватность,безопасность,бесплатный впн
```
(70 chars)

---

## URLs

- **Support URL:** `https://frkn.org/support` (или `mailto:mail@frkn.org` недопустим — нужна веб-страница; если отдельной страницы нет, используйте `https://frkn.org`)
- **Marketing URL (optional):** `https://frkn.org`
- **Privacy Policy URL:** `https://frkn.org/privacy-policy/`

---

## Age Rating (анкета)

Все вопросы про насилие/контент для взрослых/азартные игры/UGC — **Нет**.
Приложение не содержит веб-браузера → «Unrestricted Web Access» не применимо.
Ожидаемый рейтинг: **4+**.

---

## App Privacy (этикетки)

Tracking: **No** — приложение не трекерит, рекламы нет, ATT-запрос не нужен.

| Data type | Collect? | Linked to user | Purpose |
|---|---|---|---|
| Contact Info → Email Address | Yes (опционально, при триале) | Linked | App Functionality (welcome-письмо) |
| Purchases → Purchase History | Yes | Linked | App Functionality (подписка) |
| Identifiers → Device ID | Yes (installation UUID) | Linked | App Functionality (дедуп триала) |
| Diagnostics → Crash Data | No | — | — |
| Usage Data | No | — | — |
| Everything else | No | — | — |

Если на бэке появятся метрики по спеке `api-iap-backend.md` — они анонимные
(os/версия/реферал), но лучше перепроверить этикетку перед релизом.

---

## Review Notes (черновик для ревьюера)

**EN:**
```
Dopamine is a VPN client. No account is required for review:
on the setup screen tap "VPN by FRKN" → "Create trial account" (email field can
be left empty) — a trial subscription is created instantly.
To test premium: use the sandbox Apple ID — tap the premium service, choose a
plan and tap "Subscribe Now". Purchases in the review environment are not charged.
The app uses NetworkExtension (packet-tunnel) to provide the VPN connection.
```

**RU-перевод для себя:** ревьюеру не нужен аккаунт — триал создаётся без email;
премиум проверяется через sandbox; приложение использует NetworkExtension.

---

## Что приложить к версии при сабмите

- Build 4.8.14 (6) или новее
- 4 IAP-продукта (`frkn_premium_1/3/6/12_month`) — секция In-App Purchases версии
- Скриншоты 6.5" iPhone (и iPad 12.9", т.к. family = 1,2)
- Контакт для ревью + этот Review Notes
