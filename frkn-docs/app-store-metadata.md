# App Store Connect — App Information draft

Copy-paste materials for the App Store listing. Primary language: Russian;
add English localization as well (and optionally Ukrainian).

> URL-ы должны реально открываться до ревью: `https://frkn.org`,
> `https://frkn.org/privacy-policy/` (privacy policy — обязателен для VPN, гайд 5.4).

---

## Name / Subtitle

- **Name:** Dopamine by FRKN
- **Subtitle (RU, ≤30):** Приватный и безопасный VPN
- **Subtitle (EN, ≤30):** Private & secure connection

---

## Promotional Text (≤170 chars, можно менять без ревью)

**EN:**
```
Connect in one tap. Modern protocols, free servers, and full control over your privacy — no registration needed.
```

**RU:**
```
Подключение в один тап. Современные протоколы, бесплатные серверы и полный контроль над приватностью.
```

---

## Description

**EN:**
```
Dopamine is a free, open-source VPN app by FRKN.

Connect in one tap. Enter your subscription key and you're online in seconds.

WHY DOPAMINE

• Modern protocols: AmneziaWG, WireGuard, and Xray — fast and reliable
• Free and premium servers around the world
• Split tunneling — choose which apps and sites use the VPN
• KillSwitch — your traffic never leaks if the connection drops
• Import any config: QR code, key, or file. Self-hosted setups welcome
• Open source — the code is public on GitHub

PRIVACY FIRST

No ads, no trackers. Your connection is yours.

Subscription options: 1, 3, 6 or 12 months. Payment is charged to your Apple ID at confirmation. The subscription renews automatically unless auto-renew is turned off at least 24 hours before the end of the current period. Manage or cancel anytime in your Apple ID settings.

Terms of Use: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Privacy Policy: https://frkn.org/privacy-policy/
```

**RU:**
```
Dopamine — бесплатный VPN с открытым исходным кодом от FRKN.

Подключение в один тап. Введите ключ подписки — и вы в сети за секунды.

ПОЧЕМУ DOPAMINE

• Современные протоколы: AmneziaWG, WireGuard и Xray — быстро и надёжно
• Бесплатные и премиум-серверы по всему миру
• Раздельное туннелирование — выбирайте, какие приложения и сайты идут через VPN
• KillSwitch — трафик не утечёт, даже если соединение оборвётся
• Импорт любых конфигураций: QR-код, ключ или файл. Поддержка self-hosted серверов
• Открытый исходный код — весь код публичен на GitHub

ПРИВАТНОСТЬ ПРЕЖДЕ ВСЕГО

Никакой рекламы и трекеров.

Варианты подписки: 1, 3, 6 или 12 месяцев. Оплата списывается с вашего Apple ID при подтверждении. Подписка продлевается автоматически, если автопродление не отключено минимум за 24 часа до конца периода. Управление и отмена — в настройках Apple ID.

Условия использования: https://www.apple.com/legal/internet-services/itunes/dev/stdeula/
Политика конфиденциальности: https://frkn.org/privacy-policy/
```

---

## Keywords (≤100 chars, через запятую, без пробелов после запятых)

**EN:**
```
vpn,wireguard,privacy,secure,network,private network,vpn client
```

**RU:**
```
vpn,впн,wireguard,приватность,безопасность,сеть,vpn клиент
```

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
| Contact Info → Email Address | No | — | — |
| Purchases → Purchase History | Yes | Linked | App Functionality (подписка) |
| Identifiers → Device ID | Yes (installation UUID) | Linked | App Functionality |
| Diagnostics → Crash Data | No | — | — |
| Usage Data | No | — | — |
| Everything else | No | — | — |

Если на бэке появятся метрики по спеке `api-iap-backend.md` — они анонимные
(os/версия/реферал), но лучше перепроверить этикетку перед релизом.

---

## Review Notes (черновик для ревьюера)

**EN:**
```
There is no traditional username/password login.

Access to our managed VPN service uses a subscription ID on the setup screen
("VPN by FRKN"), or the deep link below.

Demo credentials for App Review:
- Username (ASC field): 8g32-k1z1-9f
- Password / subscription ID: 9c10b6ef-6df7-44f9-b68f-ba5800b0a3a0
- Deep link: frkn://sub/9c10b6ef-6df7-44f9-b68f-ba5800b0a3a0

Steps:
1. Open the app
2. On the setup screen tap "VPN by FRKN"
3. Paste the subscription ID: 9c10b6ef-6df7-44f9-b68f-ba5800b0a3a0
   (or open the deep link above)
4. Allow the iOS VPN configuration prompt
5. Connect

Premium / IAP can also be tested with a Sandbox Apple ID (purchases are not charged).
Self-hosted / imported configs do not require an account.

The app uses NetworkExtension (packet-tunnel) to provide the VPN connection.

VPN data: the tunnel does not log browsing history, destination IPs, DNS queries,
or traffic contents. Limited account/service data (subscription id, status/quota,
device/app/OS version, App Store transaction ids) is processed only to run the
service. Privacy policy: https://frkn.org/privacy-policy/
```

**ASC Demo Account fields:**
- Sign-in required: Yes
- User name: `8g32-k1z1-9f`
- Password: `9c10b6ef-6df7-44f9-b68f-ba5800b0a3a0`

**RU-перевод для себя:** ревьюеру — ID `9c10b6ef-6df7-44f9-b68f-ba5800b0a3a0`
(или deep link `frkn://sub/...`); код `8g32-k1z1-9f` в Username;
премиум — sandbox; VPN-трафик не логируется.

---

## Что приложить к версии при сабмите

- Свежий iOS build (CFBundleVersion = tweak из DOPAMINE_VERSION)
- 4 IAP-продукта (`frkn_premium_1/3/6/12_month`) — секция In-App Purchases версии
- Скриншоты 6.5" iPhone (и iPad 12.9", т.к. family = 1,2)
- Контакт для ревью + этот Review Notes с живым demo UUID
