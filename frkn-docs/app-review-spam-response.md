# App Review — ответ на реджект 4.3(a) Spam

Текст для Reply в App Store Connect (англ.). Копировать целиком.

Контекст: Apple пишет про similar binary/metadata/concept и repackaged
templates. Прошлый ответ (Aug 20) признавал «core exactly similar» — так
делать нельзя. Ниже — акцент на свой сервис, один аккаунт, proprietary
фичи и обновлённые скриншоты.

> Перед ресабмитом лучше прикрепить свежий билд (не 37), если он уже в
> TestFlight — иначе ревьюеры снова смотрят старый binary.

---

Hello,

Thank you for the review and for the clarification under Guideline 4.3(a).

Dopamine by FRKN is not a repackaged template and is not submitted across
multiple developer accounts. It is the only official iOS client of our own
commercial VPN service, operated by FRKN LLP (single Apple Developer Team:
FRKN LLP). We do not sell, white-label, or redistribute this app to other
developers.

What makes the product distinct is not only the client UI, but the service
and client features we built and operate ourselves:

1. Our own backend and subscription system
   - Own API (https://api.frkn.org) for account creation, subscription
     lifecycle, device management, and config delivery.
   - Auto-renewable subscriptions via Apple In-App Purchase, validated
     server-side with Apple's App Store Server API.
   - Deep-link onboarding: frkn://sub/<UUID> connects a user to our
     infrastructure in one step (demo credentials are in App Review
     Information).
   - Trial accounts, referrals, and device linking are handled by our
     servers — not by a third-party template backend.

2. Proprietary client features not present in a generic VPN template
   - Service-based split tunneling: curated per-service domain bundles
     (video, AI, banking, etc.) delivered dynamically from our API — users
     pick a service instead of editing subnets/domains by hand.
   - Multi-protocol stack on our infrastructure and self-maintained cores:
     AmneziaWG, VLESS (REALITY / gRPC / XHTTP), Hysteria2, and WireGuard,
     with our own connection/config layer.
   - Live server latency (ping), auto-select of the best endpoint, and
     config migration to other devices/routers that support the same
     protocols without installing our app there.
   - Fully rebranded product UI (own design system, icons, localization
     EN/RU/UK), not an Amnezia skin.

3. Open-source foundation vs. product ownership
   - Parts of the networking client descend from the Amnezia/WireGuard
     open-source ecosystem (GPLv3 permits reuse). That does not make this
     a spam clone: the commercial service, backend, IAP flow, split-tunnel
     catalogs, branding, and a large share of client logic are developed
     and operated solely by FRKN LLP.
   - Website / privacy: https://frkn.org and
     https://frkn.org/privacy-policy/

We also refreshed App Store screenshots (iPhone and iPad) to show our
current branded UI and flows, so the product page reflects the same
distinct app reviewers will see in the binary.

We respectfully request a re-evaluation under Guideline 4.3. We are happy
to provide a short call or any additional technical details you need.

Best regards,
FRKN LLP

---

## Если ответят повторным реджектом

- Appeal в App Review Board с тем же текстом.
- Можно попросить телефонный разговор с ревьюером через Contact Us.
- Усилить метаданные: описание про FRKN-сервис/свои серверы (см.
  `frkn-docs/app-store-metadata.md`), скриншоты с нашим UI.
- Приложить более новый билд с явными UI/feature diffs.
