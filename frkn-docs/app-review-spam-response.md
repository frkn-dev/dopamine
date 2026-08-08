# App Review — ответ на реджект 4.3(a) Spam

Текст для Reply в App Store Connect (англ.). Копировать целиком.

---

Hello, and thank you for the detailed review.

We'd like to clarify that Dopamine by FRKN is not a repackaged template
resold across accounts. It is the official client of our own commercial
VPN service, operated by FRKN LLP, with substantial proprietary
infrastructure and functionality that does not exist in the upstream
open-source project it is based on:

1. Our own backend and subscription system
   - Own API (api.frkn.org) for account creation, subscription management,
     device management, and config delivery.
   - Auto-renewable subscriptions sold via Apple In-App Purchase and
     validated server-side against Apple's App Store Server API.
   - Trial accounts, referral mechanics and device linking handled by our
     servers.

2. Proprietary features not present upstream
   - Service-based split tunneling: curated per-service domain bundles
     (e.g. video, AI, banking services) delivered dynamically from our API.
   - Per-connection protocol layer with our own config format: VLESS
     (REALITY, gRPC, XHTTP/CDN), AmneziaWG, Hysteria2 and plain WireGuard
     on a self-maintained fork of the xray core.
   - Our own news/presets/device endpoints and subscription reload flow.

3. Distinct product and branding
   - Fully rebranded UI (own design system, icons, animations, localized
     EN/RU/UK), redesigned server cards, diagnostics and subscription
     pages, own TestFlight beta program with external testers.

The app is published from a single developer account as the only client
of this service; it is not sold or distributed to other developers. While
the client foundation uses the open-source Amnezia codebase (GPLv3, which
permits reuse), the service, backend, subscription model and a large part
of the client logic are proprietary and developed by our team.

We'd appreciate a re-evaluation under guideline 4.3 and are happy to
provide any additional details.

---

## Если ответят повторным реджектом

- Appeal в App Review Board с тем же текстом.
- Можно попросить телефонный разговор с ревьюером через Contact Us.
- Усилить метаданные: описание про FRKN-сервис/свои серверы (см.
  `frkn-docs/app-store-metadata.md`), скриншоты с нашим UI.
